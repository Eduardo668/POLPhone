/* POLPhone - contrato de backend da fachada pública. GPL-2.0-only. */

#pragma once

#include "core/TelephonyTypes.h"
#include "util/Result.h"

#include <chrono>
#include <string_view>
#include <vector>

namespace polphone::core {

class TelephonyBackend {
public:
    virtual ~TelephonyBackend() = default;

    [[nodiscard]] virtual util::Result<void> initialize() = 0;
    [[nodiscard]] virtual util::Result<void> shutdown() = 0;
    [[nodiscard]] virtual util::Result<void> registerAccount() = 0;
    [[nodiscard]] virtual util::Result<void> unregisterAccount() = 0;
    [[nodiscard]] virtual util::Result<void> makeCall(std::string_view destination) = 0;
    [[nodiscard]] virtual util::Result<void> answerCall() = 0;
    [[nodiscard]] virtual util::Result<void> rejectCall() = 0;
    [[nodiscard]] virtual util::Result<void> hangupCall() = 0;
    [[nodiscard]] virtual util::Result<void> sendDtmf(
        std::string_view digits,
        DtmfMethod method,
        unsigned durationMs,
        unsigned gapMs) = 0;
    [[nodiscard]] virtual util::Result<void> setMuted(bool muted) = 0;
    [[nodiscard]] virtual util::Result<std::vector<AudioDevice>> listAudioDevices() = 0;
    [[nodiscard]] virtual util::Result<void> selectAudioDevice(
        AudioDirection direction,
        int id) = 0;
    [[nodiscard]] virtual util::Result<void> simulate(DemoScenario scenario) = 0;
    virtual void tick(std::chrono::milliseconds elapsed) = 0;
    [[nodiscard]] virtual TelephonySnapshot getState() const = 0;
};

} // namespace polphone::core
