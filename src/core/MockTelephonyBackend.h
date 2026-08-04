/* POLPhone - backend de demonstração sem PJSIP e sem rede. GPL-2.0-only. */

#pragma once

#include "core/TelephonyBackend.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace polphone::core {

class MockTelephonyBackend final : public TelephonyBackend {
public:
    MockTelephonyBackend();

    [[nodiscard]] util::Result<void> initialize() override;
    [[nodiscard]] util::Result<void> shutdown() override;
    [[nodiscard]] util::Result<void> registerAccount() override;
    [[nodiscard]] util::Result<void> unregisterAccount() override;
    [[nodiscard]] util::Result<void> makeCall(std::string_view destination) override;
    [[nodiscard]] util::Result<void> answerCall() override;
    [[nodiscard]] util::Result<void> rejectCall() override;
    [[nodiscard]] util::Result<void> hangupCall() override;
    [[nodiscard]] util::Result<void> applyDtmfSettings(
        const DtmfRuntimeSettings& settings) override;
    [[nodiscard]] util::Result<void> sendDtmf(
        std::string_view digits,
        DtmfMethod method) override;
    [[nodiscard]] util::Result<void> setMuted(bool muted) override;
    [[nodiscard]] util::Result<std::vector<AudioDevice>> listAudioDevices() override;
    [[nodiscard]] util::Result<void> selectAudioDevice(
        AudioDirection direction,
        int id) override;
    [[nodiscard]] util::Result<void> simulate(DemoScenario scenario) override;
    void tick(std::chrono::milliseconds elapsed) override;
    [[nodiscard]] TelephonySnapshot getState() const override;

private:
    struct ScheduledAction {
        std::uint64_t dueMs{0};
        std::function<void()> action;
    };

    void schedule(std::uint64_t delayMs, std::function<void()> action);
    void change(std::string logLine = {});
    void clearCall();
    [[nodiscard]] util::Result<void> requireReady() const;

    TelephonySnapshot state_;
    std::vector<ScheduledAction> scheduled_;
    std::uint64_t nowMs_{0};
    std::uint64_t confirmedAtMs_{0};
};

} // namespace polphone::core
