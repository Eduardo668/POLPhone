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
#include "logging/Logger.h"
#include "util/Result.h"

#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>

namespace polphone::sip {

class CallRegistry;

[[nodiscard]] util::Result<std::string> normalizeDestination(
    std::string_view destination,
    std::string_view domain);
[[nodiscard]] app::CallState callStateFromPjsip(pjsip_inv_state state) noexcept;

class SipCall final : public pj::Call {
public:
    SipCall(pj::Account& account,
            CallRegistry& registry,
            app::AppState& state,
            app::EventQueue& events,
            logging::Logger& logger,
            int callId = PJSUA_INVALID_ID);
    ~SipCall() override;

    SipCall(const SipCall&) = delete;
    SipCall& operator=(const SipCall&) = delete;

    [[nodiscard]] util::Result<void> start(std::string destinationUri);
    [[nodiscard]] util::Result<void> answer(int statusCode = PJSIP_SC_OK);
    [[nodiscard]] util::Result<void> hangupCall();
    [[nodiscard]] bool canReap() const noexcept;
    [[nodiscard]] static std::size_t liveCount() noexcept;

    void onCallState(pj::OnCallStateParam& parameter) noexcept override;
    void onCallTsxState(pj::OnCallTsxStateParam& parameter) noexcept override;

private:
    class CallbackScope final {
    public:
        explicit CallbackScope(SipCall& call) noexcept;
        ~CallbackScope();

    private:
        SipCall& call_;
    };

    void publishCallbackFailure(std::string_view callback,
                                std::string_view detail) noexcept;

    CallRegistry& registry_;
    app::AppState& state_;
    app::EventQueue& events_;
    logging::Logger& logger_;
    std::string destinationUri_;
    std::atomic<unsigned> callbackDepth_{0U};
    static std::atomic<std::size_t> liveCount_;
};

} // namespace polphone::sip
