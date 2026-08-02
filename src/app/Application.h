/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include "audio/AudioDeviceService.h"
#include "app/AppState.h"
#include "app/EventQueue.h"
#include "config/AppConfig.h"
#include "logging/Logger.h"
#include "sip/CallRegistry.h"
#include "sip/SipAccount.h"
#include "sip/SipEndpoint.h"
#include "util/Result.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace polphone::app {

class ConsoleUi;

struct ApplicationOptions {
    std::filesystem::path configPath{"config/polphone.config.json"};
    std::optional<int> consoleLogLevel;
    bool selftest{false};
    bool listDevices{false};
    bool useBuiltInConfig{false};
};

struct ApplicationStatus {
    RegistrationSnapshot registration;
    CallSnapshot call;
    std::optional<int> captureDevice;
    std::optional<int> playbackDevice;
    config::DtmfConfig dtmf;
    std::string dtmfMethod;
    int consoleLogLevel{0};
};

class Application final {
public:
    explicit Application(ApplicationOptions options);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] util::Result<void> initialize();
    [[nodiscard]] int run();
    [[nodiscard]] util::Result<void> makeCall(std::string_view destination);
    [[nodiscard]] util::Result<void> answerCall();
    [[nodiscard]] util::Result<void> hangupCall();
    [[nodiscard]] util::Result<void> setRegistrationEnabled(bool enabled);
    [[nodiscard]] util::Result<std::vector<audio::AudioDeviceDescription>>
        listAudioDevices() const;
    [[nodiscard]] util::Result<void> selectAudioDevice(
        audio::AudioDeviceDirection direction,
        int id);
    [[nodiscard]] util::Result<std::vector<sip::EffectiveCodec>> listCodecs();
    [[nodiscard]] util::Result<void> setConsoleLogLevel(int level);
    [[nodiscard]] ApplicationStatus status() const;
    [[nodiscard]] std::vector<UiEvent> drainEvents();
    [[nodiscard]] std::size_t reapCalls() noexcept;
    void shutdown() noexcept;

private:
    [[nodiscard]] util::Result<void> failInitialization(util::Error error);

    ApplicationOptions options_;
    // A ordem dos membros é a ordem de construção. O logger precisa viver
    // antes e depois do endpoint, que referencia seus sinks no PjLogWriter.
    logging::Logger logger_;
    std::shared_ptr<logging::LogSink> consoleSink_;
    AppState state_;
    EventQueue events_;
    std::optional<config::AppConfig> config_;
    std::unique_ptr<sip::SipEndpoint> endpoint_;
    std::unique_ptr<audio::AudioDeviceService> audioDevices_;
    // Deve ficar vivo enquanto SipAccount/SipCall guardarem sua referência.
    sip::CallRegistry calls_;
    std::unique_ptr<sip::SipAccount> account_;
    std::unique_ptr<ConsoleUi> console_;
    std::string dtmfMethod_;
    int consoleLogLevel_{0};
    bool initialized_{false};
    bool shutdownStarted_{false};
};

} // namespace polphone::app
