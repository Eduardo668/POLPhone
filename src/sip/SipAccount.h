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

#include "app/AppState.h"
#include "app/EventQueue.h"
#include "config/AppConfig.h"
#include "logging/Logger.h"
#include "util/Result.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <string_view>

namespace polphone::sip {

class CallRegistry;

[[nodiscard]] std::string registrationStatusMessage(
    int sipCode,
    pj_status_t transportStatus,
    std::string_view reason);

class SipAccount final : public pj::Account {
public:
    SipAccount(app::AppState& state,
               app::EventQueue& events,
               logging::Logger& logger,
               CallRegistry& calls) noexcept;
    ~SipAccount() override;

    SipAccount(const SipAccount&) = delete;
    SipAccount& operator=(const SipAccount&) = delete;

    [[nodiscard]] util::Result<void> createFrom(const config::SipConfig& config);
    [[nodiscard]] util::Result<void> setRegistrationEnabled(bool enabled);
    [[nodiscard]] util::Result<void> unregisterAndWait(
        std::chrono::milliseconds timeout = std::chrono::seconds(3));
    void shutdownAccount() noexcept;

    void onRegState(pj::OnRegStateParam& parameter) noexcept override;
    void onIncomingCall(pj::OnIncomingCallParam& parameter) noexcept override;

private:
    void publishRegistration(const pj::OnRegStateParam& parameter,
                             const pj::AccountInfo& information);
    void publishCallbackFailure(std::string_view callback,
                                std::string_view detail) noexcept;

    app::AppState& state_;
    app::EventQueue& events_;
    logging::Logger& logger_;
    CallRegistry& calls_;
    std::mutex registrationMutex_;
    std::condition_variable registrationCondition_;
    bool registrationActive_{false};
    std::atomic<bool> shuttingDown_{false};
    bool created_{false};
};

} // namespace polphone::sip
