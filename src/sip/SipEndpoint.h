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

#include <memory>
#include <string_view>

namespace polphone::sip {

class SipEndpoint final {
public:
    SipEndpoint() = default;
    ~SipEndpoint();

    SipEndpoint(const SipEndpoint&) = delete;
    SipEndpoint& operator=(const SipEndpoint&) = delete;

    [[nodiscard]] util::Result<void> create();
    [[nodiscard]] util::Result<void> init(const config::AppConfig& config,
                                          logging::Logger& logger,
                                          int technicalLogLevel);
    [[nodiscard]] util::Result<void> createUdpTransport(
        const config::NetworkConfig& config);
    [[nodiscard]] util::Result<void> start();
    [[nodiscard]] util::Result<void> applyCodecPriorities(
        const config::CodecsConfig& config);
    [[nodiscard]] util::Result<void> registerThisThread(std::string_view name);
    [[nodiscard]] util::Result<void> hangupAllCalls();
    [[nodiscard]] util::Result<void> destroy() noexcept;

    [[nodiscard]] bool isCreated() const noexcept;
    [[nodiscard]] bool isStarted() const noexcept;
    [[nodiscard]] pj::Endpoint* native() noexcept;

private:
    enum class State {
        Empty,
        Created,
        Initialized,
        TransportReady,
        Started
    };

    [[nodiscard]] util::Result<void> invalidState(std::string_view operation) const;

    std::unique_ptr<pj::Endpoint> endpoint_;
    logging::Logger* logger_{nullptr};
    pj::TransportId transportId_{pj::INVALID_ID};
    State state_{State::Empty};
};

} // namespace polphone::sip
