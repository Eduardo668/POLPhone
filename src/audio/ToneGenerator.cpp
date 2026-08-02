/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "audio/ToneGenerator.h"

#include "sip/PjErrors.h"

#include <pjmedia/tonegen.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace polphone::audio {
namespace {

util::Error toneError(std::string message, pj_status_t status)
{
    return util::Error{
        util::ErrorCode::Pjsip,
        std::move(message),
        "status=" + std::to_string(status) + " ("
            + sip::statusToString(status) + ")"};
}

} // namespace

short toneAmplitudeFromDbm0(int volumeDbm0) noexcept
{
    const int clamped = (std::clamp)(volumeDbm0, -30, 0);
    const double ratio = std::pow(10.0, static_cast<double>(clamped) / 20.0);
    const long amplitude = std::lround(32767.0 * ratio);
    return static_cast<short>((std::clamp)(amplitude, 1L, 32767L));
}

ToneGenerator::~ToneGenerator()
{
    destroy();
}

util::Result<void> ToneGenerator::create(
    unsigned clockRate,
    unsigned channelCount,
    unsigned ptimeMs)
{
    if (clockRate == 0U || (channelCount != 1U && channelCount != 2U)
        || ptimeMs == 0U || ptimeMs > 1000U) {
        return util::Result<void>::failure(
            util::ErrorCode::InvalidArgument,
            "Parâmetros inválidos para o gerador de tons DTMF.");
    }
    if (port_ != nullptr) {
        if (clockRate_ == clockRate && channelCount_ == channelCount
            && ptimeMs_ == ptimeMs) {
            return util::Result<void>::success();
        }
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "O gerador DTMF já foi criado com outro formato de áudio.");
    }
    if (clockRate > (std::numeric_limits<unsigned>::max)() / ptimeMs
        || clockRate * ptimeMs / 1000U
            > (std::numeric_limits<unsigned>::max)() / channelCount) {
        return util::Result<void>::failure(
            util::ErrorCode::InvalidArgument,
            "O formato do gerador DTMF excede os limites suportados.");
    }

    try {
        pool_ = pjsua_pool_create("polphone-tonegen", 1024U, 1024U);
        if (pool_ == nullptr) {
            return util::Result<void>::failure(
                util::ErrorCode::Runtime,
                "Não foi possível criar o pool do gerador DTMF.");
        }
        const unsigned samplesPerFrame =
            clockRate * ptimeMs / 1000U * channelCount;
        if (samplesPerFrame == 0U) {
            destroy();
            return util::Result<void>::failure(
                util::ErrorCode::InvalidArgument,
                "O formato de áudio produz zero amostras por frame DTMF.");
        }
        pj_str_t name = pj_str(const_cast<char*>("polphone-tonegen"));
        const pj_status_t created = pjmedia_tonegen_create2(
            pool_,
            &name,
            clockRate,
            channelCount,
            samplesPerFrame,
            16U,
            0U,
            &port_);
        if (created != PJ_SUCCESS) {
            const util::Error error = toneError(
                "Não foi possível criar a porta do gerador DTMF.", created);
            destroy();
            return util::Result<void>::failure(error);
        }
        registerMediaPort2(static_cast<pj::MediaPort>(port_), pool_);
        registered_ = true;
        clockRate_ = clockRate;
        channelCount_ = channelCount;
        ptimeMs_ = ptimeMs;
        return util::Result<void>::success();
    } catch (const pj::Error& error) {
        destroy();
        return util::Result<void>::failure(
            sip::makePjError(error, "registrar gerador DTMF na conference bridge"));
    } catch (const std::exception& error) {
        destroy();
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Falha ao criar o gerador DTMF.",
            error.what());
    } catch (...) {
        destroy();
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Falha desconhecida ao criar o gerador DTMF.");
    }
}

util::Result<void> ToneGenerator::connect(
    const pj::AudioMedia& callMedia,
    bool localFeedback)
{
    if (port_ == nullptr || !registered_) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "O gerador DTMF ainda não foi criado.");
    }
    disconnect();
    try {
        startTransmit(callMedia);
        callMedia_ = callMedia;
        if (localFeedback) {
            pj::AudioMedia& playback =
                pj::Endpoint::instance().audDevManager().getPlaybackDevMedia();
            startTransmit(playback);
            playbackMedia_ = playback;
        }
        return util::Result<void>::success();
    } catch (const pj::Error& error) {
        disconnect();
        return util::Result<void>::failure(
            sip::makePjError(error, "conectar gerador DTMF à chamada"));
    } catch (const std::exception& error) {
        disconnect();
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Falha ao conectar o gerador DTMF à chamada.",
            error.what());
    } catch (...) {
        disconnect();
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Falha desconhecida ao conectar o gerador DTMF à chamada.");
    }
}

util::Result<void> ToneGenerator::playDigits(
    const std::vector<ToneDigit>& digits)
{
    if (port_ == nullptr || digits.empty()) {
        return util::Result<void>::failure(
            util::ErrorCode::InvalidArgument,
            "O gerador DTMF requer ao menos um dígito e uma porta ativa.");
    }
    if (digits.size() > MaxQueuedDigits) {
        return util::Result<void>::failure(
            util::ErrorCode::InvalidArgument,
            "A sequência excede a fila interna do gerador DTMF.",
            "max=" + std::to_string(MaxQueuedDigits));
    }
    if (isBusy()) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "O gerador DTMF ainda está ocupado com a sequência anterior.");
    }
    try {
        std::vector<pjmedia_tone_digit> native;
        native.reserve(digits.size());
        for (const auto& digit : digits) {
            if (digit.onMs > static_cast<unsigned>((std::numeric_limits<short>::max)())
                || digit.offMs > static_cast<unsigned>((std::numeric_limits<short>::max)())) {
                return util::Result<void>::failure(
                    util::ErrorCode::InvalidArgument,
                    "A temporização do tom DTMF excede o limite do PJMEDIA.");
            }
            native.push_back(pjmedia_tone_digit{
                digit.digit,
                static_cast<short>(digit.onMs),
                static_cast<short>(digit.offMs),
                toneAmplitudeFromDbm0(digit.volumeDbm0)});
        }
        const pj_status_t played = pjmedia_tonegen_play_digits(
            port_, static_cast<unsigned>(native.size()), native.data(), 0U);
        if (played != PJ_SUCCESS) {
            return util::Result<void>::failure(toneError(
                "O PJMEDIA recusou a sequência DTMF in-band.", played));
        }
        return util::Result<void>::success();
    } catch (const std::exception& error) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Falha ao preparar a sequência DTMF in-band.",
            error.what());
    } catch (...) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Falha desconhecida ao preparar a sequência DTMF in-band.");
    }
}

util::Result<void> ToneGenerator::stop()
{
    if (port_ == nullptr) return util::Result<void>::success();
    const pj_status_t stopped = pjmedia_tonegen_stop(port_);
    if (stopped != PJ_SUCCESS) {
        return util::Result<void>::failure(
            toneError("Não foi possível interromper o gerador DTMF.", stopped));
    }
    return util::Result<void>::success();
}

bool ToneGenerator::isBusy() const noexcept
{
    return port_ != nullptr && pjmedia_tonegen_is_busy(port_) != 0;
}

int ToneGenerator::bridgeSlot() const noexcept
{
    return registered_ ? getPortId() : PJSUA_INVALID_ID;
}

void ToneGenerator::disconnect() noexcept
{
    try {
        if (playbackMedia_.has_value()) {
            try {
                stopTransmit(*playbackMedia_);
            } catch (...) {
            }
            playbackMedia_.reset();
        }
        if (callMedia_.has_value()) {
            try {
                stopTransmit(*callMedia_);
            } catch (...) {
            }
            callMedia_.reset();
        }
    } catch (...) {
        playbackMedia_.reset();
        callMedia_.reset();
    }
}

void ToneGenerator::destroy() noexcept
{
    disconnect();
    if (port_ != nullptr) {
        static_cast<void>(pjmedia_tonegen_stop(port_));
    }
    if (registered_) {
        try {
            unregisterMediaPort();
        } catch (...) {
        }
        registered_ = false;
    }
    if (port_ != nullptr) {
        static_cast<void>(pjmedia_port_destroy(port_));
        port_ = nullptr;
    }
    if (pool_ != nullptr) {
        pj_pool_release(pool_);
        pool_ = nullptr;
    }
    clockRate_ = 0U;
    channelCount_ = 0U;
    ptimeMs_ = 0U;
}

} // namespace polphone::audio
