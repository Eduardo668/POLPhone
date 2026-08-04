/* POLPhone - adaptador do motor SIP real para a fachada pública. GPL-2.0-only. */

#pragma once

#include "app/Application.h"
#include "core/TelephonyBackend.h"

#include <chrono>
#include <filesystem>
#include <memory>

namespace polphone::core {

class RealTelephonyBackend final : public TelephonyBackend {
public:
    explicit RealTelephonyBackend(std::filesystem::path configPath);

    [[nodiscard]] util::Result<void> initialize() override;
    [[nodiscard]] util::Result<void> shutdown() override;
    [[nodiscard]] util::Result<void> registerAccount() override;
    [[nodiscard]] util::Result<void> unregisterAccount() override;
    [[nodiscard]] util::Result<void> makeCall(std::string_view destination) override;
    [[nodiscard]] util::Result<void> answerCall() override;
    [[nodiscard]] util::Result<void> rejectCall() override;
    [[nodiscard]] util::Result<void> hangupCall() override;
    [[nodiscard]] util::Result<void> sendDtmf(
        std::string_view digits,
        DtmfMethod method,
        unsigned durationMs,
        unsigned gapMs) override;
    [[nodiscard]] util::Result<void> setMuted(bool muted) override;
    [[nodiscard]] util::Result<std::vector<AudioDevice>> listAudioDevices() override;
    [[nodiscard]] util::Result<void> selectAudioDevice(
        AudioDirection direction,
        int id) override;
    [[nodiscard]] util::Result<void> simulate(DemoScenario scenario) override;
    void tick(std::chrono::milliseconds elapsed) override;
    [[nodiscard]] TelephonySnapshot getState() const override;

private:
    void refresh();
    void recordError(const util::Error& error);

    app::ApplicationOptions options_;
    std::unique_ptr<app::Application> application_;
    TelephonySnapshot state_;
    std::chrono::milliseconds confirmedDuration_{0};
};

} // namespace polphone::core
