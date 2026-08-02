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
#include <string_view>

namespace polphone::app {

struct ApplicationOptions {
    std::filesystem::path configPath{"config/polphone.config.json"};
    std::optional<int> consoleLogLevel;
    bool selftest{false};
    bool listDevices{false};
    bool useBuiltInConfig{false};
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
    void shutdown() noexcept;

private:
    [[nodiscard]] util::Result<void> failInitialization(util::Error error);

    ApplicationOptions options_;
    // A ordem dos membros é a ordem de construção. O logger precisa viver
    // antes e depois do endpoint, que referencia seus sinks no PjLogWriter.
    logging::Logger logger_;
    AppState state_;
    EventQueue events_;
    std::optional<config::AppConfig> config_;
    std::unique_ptr<sip::SipEndpoint> endpoint_;
    std::unique_ptr<audio::AudioDeviceService> audioDevices_;
    // Deve ficar vivo enquanto SipAccount/SipCall guardarem sua referência.
    sip::CallRegistry calls_;
    std::unique_ptr<sip::SipAccount> account_;
    bool initialized_{false};
    bool shutdownStarted_{false};
};

} // namespace polphone::app
