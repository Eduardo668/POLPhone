/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "logging/PjLogWriter.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <limits>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace polphone::logging {
namespace {

thread_local bool gWritingPjsipLog = false;

class CallbackGuard final {
public:
    CallbackGuard() noexcept
    {
        if (!gWritingPjsipLog) {
            gWritingPjsipLog = true;
            entered_ = true;
        }
    }

    ~CallbackGuard()
    {
        if (entered_) {
            gWritingPjsipLog = false;
        }
    }

    [[nodiscard]] bool entered() const noexcept { return entered_; }

private:
    bool entered_{false};
};

std::string normalizeNewlines(std::string_view input)
{
    std::string normalized;
    normalized.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] == '\r') {
            normalized.push_back('\n');
            if (index + 1U < input.size() && input[index + 1U] == '\n') {
                ++index;
            }
        } else {
            normalized.push_back(input[index]);
        }
    }
    while (!normalized.empty() && normalized.back() == '\n') {
        normalized.pop_back();
    }
    return normalized;
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
        if (index + continuationCount >= input.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset <= continuationCount; ++offset) {
            const auto next = static_cast<unsigned char>(input[index + offset]);
            if ((next & 0xC0U) != 0x80U) {
                return false;
            }
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

std::string replaceNonAscii(std::string_view input)
{
    std::string safe(input);
    for (char& value : safe) {
        if (static_cast<unsigned char>(value) > 0x7FU) {
            value = '?';
        }
    }
    return safe;
}

std::string pjsipTextToUtf8(std::string_view input)
{
    // A maior parte das mensagens do PJSIP já é UTF-8. O backend WMME, porém,
    // pode inserir nomes de dispositivos na code page ANSI do Windows.
    if (input.empty() || isValidUtf8(input)) {
        return std::string(input);
    }

#ifdef _WIN32
    if (input.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return replaceNonAscii(input);
    }
    const int inputSize = static_cast<int>(input.size());
    const int wideSize = MultiByteToWideChar(
        CP_ACP, MB_ERR_INVALID_CHARS, input.data(), inputSize, nullptr, 0);
    if (wideSize <= 0) {
        return replaceNonAscii(input);
    }
    std::vector<wchar_t> wide(static_cast<std::size_t>(wideSize));
    if (MultiByteToWideChar(
            CP_ACP, MB_ERR_INVALID_CHARS, input.data(), inputSize, wide.data(), wideSize)
        != wideSize) {
        return replaceNonAscii(input);
    }
    const int utf8Size = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), wideSize, nullptr, 0, nullptr, nullptr);
    if (utf8Size <= 0) {
        return replaceNonAscii(input);
    }
    std::string utf8(static_cast<std::size_t>(utf8Size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, 0, wide.data(), wideSize,
            utf8.data(), utf8Size, nullptr, nullptr)
        != utf8Size) {
        return replaceNonAscii(input);
    }
    return utf8;
#else
    return replaceNonAscii(input);
#endif
}

} // namespace

PjLogWriter::PjLogWriter(Logger& logger) noexcept
    : logger_(logger)
{
}

LogLevel PjLogWriter::mapLevel(int pjsipLevel) noexcept
{
    return logLevelFromNumber(pjsipLevel);
}

void PjLogWriter::write(const pj::LogEntry& entry) noexcept
{
    CallbackGuard guard;
    if (!guard.entered()) {
        return;
    }

    try {
        const LogLevel level = mapLevel(entry.level);
        const std::string normalized = normalizeNewlines(pjsipTextToUtf8(entry.msg));
        if (normalized.empty()) {
            static_cast<void>(logger_.log(level, "pjsip", ""));
            return;
        }

        std::size_t begin = 0;
        while (begin <= normalized.size()) {
            const std::size_t end = normalized.find('\n', begin);
            const std::string_view line(
                normalized.data() + begin,
                end == std::string::npos ? normalized.size() - begin : end - begin);
            static_cast<void>(logger_.log(level, "pjsip", line));
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1U;
        }
    } catch (const pj::Error&) {
        // A callback não pode propagar falhas para o código C do PJSIP.
    } catch (const std::exception&) {
        // A callback não pode propagar falhas para o código C do PJSIP.
    } catch (...) {
        // A callback não pode propagar falhas para o código C do PJSIP.
    }
}

} // namespace polphone::logging
