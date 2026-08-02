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

#include "config/AppConfig.h"
#include "logging/Logger.h"
#include "util/Result.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace polphone::audio {

enum class AudioDeviceDirection {
    Capture,
    Playback
};

struct AudioDeviceDescription {
    int id{-1};
    std::string name;
    unsigned inputCount{0};
    unsigned outputCount{0};
    unsigned defaultSamplesPerSec{0};
    std::string driver;

    [[nodiscard]] bool supports(AudioDeviceDirection direction) const noexcept;
};

class AudioDeviceService final {
public:
    AudioDeviceService(pj::Endpoint& endpoint, logging::Logger& logger) noexcept;

    [[nodiscard]] util::Result<std::vector<AudioDeviceDescription>> list() const;
    [[nodiscard]] static util::Result<int> resolveByName(
        const std::vector<AudioDeviceDescription>& devices,
        std::string_view name,
        AudioDeviceDirection direction);

    [[nodiscard]] util::Result<void> apply(const config::AudioConfig& config,
                                           bool callActive = false);
    [[nodiscard]] util::Result<void> selectCapture(int id, bool callActive = false);
    [[nodiscard]] util::Result<void> selectPlayback(int id, bool callActive = false);

    [[nodiscard]] std::optional<int> selectedCapture() const noexcept;
    [[nodiscard]] std::optional<int> selectedPlayback() const noexcept;

private:
    [[nodiscard]] util::Result<void> select(AudioDeviceDirection direction,
                                            int id,
                                            bool callActive);
    [[nodiscard]] util::Result<void> applyOne(
        const std::vector<AudioDeviceDescription>& devices,
        AudioDeviceDirection direction,
        std::string_view configuredValue,
        bool callActive);
    void logEffective(const std::vector<AudioDeviceDescription>& devices,
                      AudioDeviceDirection direction,
                      std::optional<int> selected) const noexcept;

    pj::Endpoint& endpoint_;
    logging::Logger& logger_;
    std::optional<int> captureId_;
    std::optional<int> playbackId_;
};

} // namespace polphone::audio
