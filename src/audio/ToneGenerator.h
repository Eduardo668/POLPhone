/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

// O PJSIP deve vir antes dos headers do Windows para evitar conflito de Winsock.
#include <pjsua2.hpp>

#include "util/Result.h"

#include <cstddef>
#include <optional>
#include <vector>

struct pj_pool_t;
struct pjmedia_port;

namespace polphone::audio {

struct ToneDigit {
    char digit{'\0'};
    unsigned onMs{0U};
    unsigned offMs{0U};
    int volumeDbm0{-10};
};

[[nodiscard]] short toneAmplitudeFromDbm0(int volumeDbm0) noexcept;

class ToneGenerator final : private pj::AudioMedia {
public:
    static constexpr std::size_t MaxQueuedDigits = PJMEDIA_TONEGEN_MAX_DIGITS;

    ToneGenerator() = default;
    ~ToneGenerator() override;

    ToneGenerator(const ToneGenerator&) = delete;
    ToneGenerator& operator=(const ToneGenerator&) = delete;

    [[nodiscard]] util::Result<void> create(
        unsigned clockRate,
        unsigned channelCount,
        unsigned ptimeMs);
    [[nodiscard]] util::Result<void> connect(
        const pj::AudioMedia& callMedia,
        bool localFeedback);
    [[nodiscard]] util::Result<void> playDigits(
        const std::vector<ToneDigit>& digits);
    [[nodiscard]] util::Result<void> stop();
    [[nodiscard]] bool isBusy() const noexcept;
    [[nodiscard]] int bridgeSlot() const noexcept;
    void disconnect() noexcept;

private:
    void destroy() noexcept;

    pj_pool_t* pool_{nullptr};
    pjmedia_port* port_{nullptr};
    std::optional<pj::AudioMedia> callMedia_;
    std::optional<pj::AudioMedia> playbackMedia_;
    unsigned clockRate_{0U};
    unsigned channelCount_{0U};
    unsigned ptimeMs_{0U};
    bool registered_{false};
};

} // namespace polphone::audio
