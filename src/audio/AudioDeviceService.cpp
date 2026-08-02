/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "audio/AudioDeviceService.h"

#include "sip/PjErrors.h"
#include "util/Strings.h"

#include <pjmedia-audiodev/errno.h>

#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace polphone::audio {
namespace {

constexpr std::size_t kWmmeNameLimit = 31U;

std::string directionName(AudioDeviceDirection direction)
{
    return direction == AudioDeviceDirection::Capture ? "captura" : "reprodução";
}

char lowerAscii(char value) noexcept
{
    if (value >= 'A' && value <= 'Z') return static_cast<char>(value - 'A' + 'a');
    return value;
}

std::string lowerAsciiCopy(std::string_view value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), lowerAscii);
    return result;
}

bool isValidUtf8(std::string_view input) noexcept
{
    std::size_t index = 0;
    while (index < input.size()) {
        const auto first = static_cast<unsigned char>(input[index]);
        std::size_t continuationCount = 0;
        unsigned codePoint = 0;
        if (first <= 0x7FU) {
            ++index;
            continue;
        }
        if (first >= 0xC2U && first <= 0xDFU) {
            continuationCount = 1;
            codePoint = first & 0x1FU;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            continuationCount = 2;
            codePoint = first & 0x0FU;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            continuationCount = 3;
            codePoint = first & 0x07U;
        } else {
            return false;
        }
        if (index + continuationCount >= input.size()) return false;
        for (std::size_t offset = 1; offset <= continuationCount; ++offset) {
            const auto next = static_cast<unsigned char>(input[index + offset]);
            if ((next & 0xC0U) != 0x80U) return false;
            codePoint = (codePoint << 6U) | (next & 0x3FU);
        }
        const bool overlong = (continuationCount == 1U && codePoint < 0x80U)
            || (continuationCount == 2U && codePoint < 0x800U)
            || (continuationCount == 3U && codePoint < 0x10000U);
        if (overlong || (codePoint >= 0xD800U && codePoint <= 0xDFFFU)
            || codePoint > 0x10FFFFU) {
            return false;
        }
        index += continuationCount + 1U;
    }
    return true;
}

std::string systemTextToUtf8(std::string_view input)
{
    if (input.empty() || isValidUtf8(input)) return std::string(input);
    if (input.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return std::string(input);
    }
    const int inputSize = static_cast<int>(input.size());
    const int wideSize = MultiByteToWideChar(
        CP_ACP, MB_ERR_INVALID_CHARS, input.data(), inputSize, nullptr, 0);
    if (wideSize <= 0) return std::string(input);
    std::vector<wchar_t> wide(static_cast<std::size_t>(wideSize));
    if (MultiByteToWideChar(
            CP_ACP, MB_ERR_INVALID_CHARS, input.data(), inputSize, wide.data(), wideSize)
        != wideSize) {
        return std::string(input);
    }
    const int utf8Size = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), wideSize, nullptr, 0, nullptr, nullptr);
    if (utf8Size <= 0) return std::string(input);
    std::string result(static_cast<std::size_t>(utf8Size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, 0, wide.data(), wideSize, result.data(), utf8Size, nullptr, nullptr)
        != utf8Size) {
        return std::string(input);
    }
    return result;
}

std::optional<int> parseDeviceId(std::string_view value)
{
    if (value.size() < 2U || value.front() != '#') return std::nullopt;
    int id = -1;
    const char* begin = value.data() + 1;
    const char* end = value.data() + value.size();
    const auto parsed = std::from_chars(begin, end, id);
    if (parsed.ec != std::errc{} || parsed.ptr != end || id < 0) return std::nullopt;
    return id;
}

util::Error noDevicesErrorDetails()
{
    return util::Error{
        util::ErrorCode::Pjsip,
        "Nenhum dispositivo de áudio padrão. Verifique devices e audio.captureDevice.",
        "PJMEDIA_EAUD_NODEFDEV"};
}

util::Result<void> noDevicesError()
{
    return util::Result<void>::failure(noDevicesErrorDetails());
}

bool isNoDefaultDeviceError(const util::Error& error)
{
    return error.detail.find("status=" + std::to_string(PJMEDIA_EAUD_NODEFDEV))
        != std::string::npos;
}

} // namespace

bool AudioDeviceDescription::supports(AudioDeviceDirection direction) const noexcept
{
    return direction == AudioDeviceDirection::Capture ? inputCount > 0U : outputCount > 0U;
}

AudioDeviceService::AudioDeviceService(pj::Endpoint& endpoint, logging::Logger& logger) noexcept
    : endpoint_(endpoint), logger_(logger)
{
}

util::Result<std::vector<AudioDeviceDescription>> AudioDeviceService::list() const
{
    const auto enumerated = POLPHONE_PJ_TRY(endpoint_.audDevManager().enumDev2());
    if (!enumerated) {
        if (isNoDefaultDeviceError(enumerated.error())) {
            return util::Result<std::vector<AudioDeviceDescription>>::failure(
                noDevicesErrorDetails());
        }
        return util::Result<std::vector<AudioDeviceDescription>>::failure(enumerated.error());
    }

    std::vector<AudioDeviceDescription> devices;
    devices.reserve(enumerated.value().size());
    for (const auto& device : enumerated.value()) {
        devices.push_back(AudioDeviceDescription{
            static_cast<int>(device.id),
            systemTextToUtf8(device.name),
            device.inputCount,
            device.outputCount,
            device.defaultSamplesPerSec,
            systemTextToUtf8(device.driver)});
    }
    return util::Result<std::vector<AudioDeviceDescription>>::success(std::move(devices));
}

util::Result<int> AudioDeviceService::resolveByName(
    const std::vector<AudioDeviceDescription>& devices,
    std::string_view name,
    AudioDeviceDirection direction)
{
    const std::string query = lowerAsciiCopy(util::trim(name));
    if (query.empty()) {
        return util::Result<int>::failure(
            util::ErrorCode::InvalidArgument,
            "Informe parte do nome do dispositivo.",
            directionName(direction));
    }

    std::vector<int> matches;
    for (const auto& device : devices) {
        if (!device.supports(direction)) continue;
        const std::string candidate = lowerAsciiCopy(device.name);
        const bool substringMatch = candidate.find(query) != std::string::npos;
        const bool truncatedWmmeMatch = candidate.size() >= kWmmeNameLimit
            && query.size() > candidate.size()
            && query.find(candidate) != std::string::npos;
        if (substringMatch || truncatedWmmeMatch) matches.push_back(device.id);
    }

    if (matches.empty()) {
        return util::Result<int>::failure(
            util::ErrorCode::NotFound,
            "Nenhum dispositivo de " + directionName(direction)
                + " corresponde ao nome informado.",
            std::string(name));
    }
    if (matches.size() > 1U) {
        return util::Result<int>::failure(
            util::ErrorCode::InvalidArgument,
            "O nome informado corresponde a mais de um dispositivo; use um trecho mais específico ou #<id>.",
            std::string(name));
    }
    return util::Result<int>::success(matches.front());
}

util::Result<void> AudioDeviceService::select(
    AudioDeviceDirection direction,
    int id,
    bool callActive)
{
    if (callActive) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "A troca de dispositivo é recusada durante uma chamada ativa; encerre a chamada e tente novamente.",
            directionName(direction));
    }

    const auto devices = list();
    if (!devices) return util::Result<void>::failure(devices.error());
    const auto found = std::find_if(
        devices.value().cbegin(), devices.value().cend(),
        [id, direction](const AudioDeviceDescription& device) {
            return device.id == id && device.supports(direction);
        });
    if (found == devices.value().cend()) {
        return util::Result<void>::failure(
            util::ErrorCode::NotFound,
            "O dispositivo de " + directionName(direction)
                + " não existe ou não suporta essa direção.",
            "id=" + std::to_string(id));
    }

    const auto applied = direction == AudioDeviceDirection::Capture
        ? POLPHONE_PJ_TRY(endpoint_.audDevManager().setCaptureDev(id))
        : POLPHONE_PJ_TRY(endpoint_.audDevManager().setPlaybackDev(id));
    if (!applied) {
        if (isNoDefaultDeviceError(applied.error())) return noDevicesError();
        return applied;
    }

    if (direction == AudioDeviceDirection::Capture) captureId_ = id;
    else playbackId_ = id;
    static_cast<void>(logger_.info(
        "audio",
        "Dispositivo de " + directionName(direction) + " selecionado: id="
            + std::to_string(found->id) + " name=\"" + found->name + "\" driver="
            + found->driver + "."));
    return util::Result<void>::success();
}

util::Result<void> AudioDeviceService::applyOne(
    const std::vector<AudioDeviceDescription>& devices,
    AudioDeviceDirection direction,
    std::string_view configuredValue,
    bool callActive)
{
    const std::string value = util::trim(configuredValue);
    if (value.empty()) {
        logEffective(devices, direction, std::nullopt);
        return util::Result<void>::success();
    }

    std::optional<int> id = parseDeviceId(value);
    util::Result<int> resolved = id.has_value()
        ? util::Result<int>::success(*id)
        : resolveByName(devices, value, direction);
    if (!resolved) {
        static_cast<void>(logger_.warning(
            "audio",
            resolved.error().message + " Usando o dispositivo padrão do sistema. detalhe="
                + resolved.error().detail));
        logEffective(devices, direction, std::nullopt);
        return util::Result<void>::success();
    }

    const auto selected = select(direction, resolved.value(), callActive);
    if (!selected && selected.error().code == util::ErrorCode::NotFound) {
        static_cast<void>(logger_.warning(
            "audio",
            selected.error().message + " Usando o dispositivo padrão do sistema. detalhe="
                + selected.error().detail));
        logEffective(devices, direction, std::nullopt);
        return util::Result<void>::success();
    }
    return selected;
}

util::Result<void> AudioDeviceService::apply(const config::AudioConfig& config,
                                             bool callActive)
{
    const auto devices = list();
    if (!devices) return util::Result<void>::failure(devices.error());
    if (devices.value().empty()) return noDevicesError();

    const auto capture = applyOne(
        devices.value(), AudioDeviceDirection::Capture, config.captureDevice, callActive);
    if (!capture) return capture;
    return applyOne(
        devices.value(), AudioDeviceDirection::Playback, config.playbackDevice, callActive);
}

util::Result<void> AudioDeviceService::selectCapture(int id, bool callActive)
{
    return select(AudioDeviceDirection::Capture, id, callActive);
}

util::Result<void> AudioDeviceService::selectPlayback(int id, bool callActive)
{
    return select(AudioDeviceDirection::Playback, id, callActive);
}

std::optional<int> AudioDeviceService::selectedCapture() const noexcept
{
    return captureId_;
}

std::optional<int> AudioDeviceService::selectedPlayback() const noexcept
{
    return playbackId_;
}

void AudioDeviceService::logEffective(
    const std::vector<AudioDeviceDescription>& devices,
    AudioDeviceDirection direction,
    std::optional<int> selected) const noexcept
{
    if (selected.has_value()) {
        const auto found = std::find_if(
            devices.cbegin(), devices.cend(),
            [selected](const AudioDeviceDescription& device) {
                return device.id == *selected;
            });
        if (found != devices.cend()) {
            static_cast<void>(logger_.info(
                "audio",
                "Dispositivo efetivo de " + directionName(direction) + ": id="
                    + std::to_string(found->id) + " name=\"" + found->name + "\"."));
            return;
        }
    }
    static_cast<void>(logger_.info(
        "audio",
        "Dispositivo efetivo de " + directionName(direction)
            + ": padrão automático do sistema (id=-1)."));
}

} // namespace polphone::audio
