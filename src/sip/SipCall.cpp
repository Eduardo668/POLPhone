/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "sip/SipCall.h"

#include "sip/CallRegistry.h"
#include "sip/PjErrors.h"
#include "util/Strings.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <string>
#include <utility>

namespace polphone::sip {
namespace {

bool isDialCharacter(char value) noexcept
{
    return (value >= '0' && value <= '9') || value == '*' || value == '#'
        || value == '+';
}

bool startsWithSipScheme(std::string_view value) noexcept
{
    if (value.size() < 4U) return false;
    const auto equalAscii = [](char left, char right) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(left))) == right;
    };
    return equalAscii(value[0], 's') && equalAscii(value[1], 'i')
        && equalAscii(value[2], 'p')
        && (value[3] == ':'
            || (value.size() >= 5U && equalAscii(value[3], 's') && value[4] == ':'));
}

std::string callStateText(app::CallState state)
{
    switch (state) {
    case app::CallState::Idle: return "IDLE";
    case app::CallState::Incoming: return "INCOMING";
    case app::CallState::Calling: return "CALLING";
    case app::CallState::Early: return "EARLY";
    case app::CallState::Connecting: return "CONNECTING";
    case app::CallState::Confirmed: return "CONFIRMED";
    case app::CallState::Disconnected: return "DISCONNECTED";
    }
    return "UNKNOWN";
}

} // namespace

std::atomic<std::size_t> SipCall::liveCount_{0U};

util::Result<std::string> normalizeDestination(
    std::string_view destination,
    std::string_view domain)
{
    const std::string normalized = util::trim(destination);
    if (normalized.empty()) {
        return util::Result<std::string>::failure(
            util::ErrorCode::InvalidArgument,
            "Informe um destino SIP ou um número para discar.");
    }
    if (startsWithSipScheme(normalized)) {
        return util::Result<std::string>::success(normalized);
    }
    if (!std::all_of(normalized.cbegin(), normalized.cend(), isDialCharacter)) {
        return util::Result<std::string>::failure(
            util::ErrorCode::InvalidArgument,
            "Destino inválido; use somente dígitos, *, #, + ou uma URI sip:/sips: completa.",
            normalized);
    }
    const std::string normalizedDomain = util::trim(domain);
    if (normalizedDomain.empty()) {
        return util::Result<std::string>::failure(
            util::ErrorCode::Validation,
            "O domínio SIP está vazio; configure sip.domain antes de discar.");
    }
    return util::Result<std::string>::success(
        "sip:" + normalized + "@" + normalizedDomain);
}

app::CallState callStateFromPjsip(pjsip_inv_state state) noexcept
{
    switch (state) {
    case PJSIP_INV_STATE_INCOMING: return app::CallState::Incoming;
    case PJSIP_INV_STATE_CALLING: return app::CallState::Calling;
    case PJSIP_INV_STATE_EARLY: return app::CallState::Early;
    case PJSIP_INV_STATE_CONNECTING: return app::CallState::Connecting;
    case PJSIP_INV_STATE_CONFIRMED: return app::CallState::Confirmed;
    case PJSIP_INV_STATE_DISCONNECTED: return app::CallState::Disconnected;
    case PJSIP_INV_STATE_NULL:
    default:
        return app::CallState::Idle;
    }
}

SipCall::SipCall(pj::Account& account,
                 CallRegistry& registry,
                 app::AppState& state,
                 app::EventQueue& events,
                 logging::Logger& logger,
                 int callId)
    : pj::Call(account, callId),
      registry_(registry),
      state_(state),
      events_(events),
      logger_(logger)
{
    ++liveCount_;
    try {
        static_cast<void>(logger_.debug(
            "call", "SipCall criado; objetos vivos=" + std::to_string(liveCount())));
    } catch (...) {
    }
}

SipCall::~SipCall()
{
    --liveCount_;
    try {
        static_cast<void>(logger_.debug(
            "call", "SipCall destruído; objetos vivos=" + std::to_string(liveCount())));
    } catch (...) {
    }
}

util::Result<void> SipCall::start(std::string destinationUri)
{
    destinationUri_ = std::move(destinationUri);
    pj::CallOpParam parameter(true);
    const auto started = POLPHONE_PJ_TRY(makeCall(destinationUri_, parameter));
    if (!started) return started;
    state_.updateCall(app::CallSnapshot{
        app::CallState::Calling, 0, {}, destinationUri_});
    return util::Result<void>::success();
}

util::Result<void> SipCall::answer(int statusCode)
{
    pj::CallOpParam parameter(true);
    parameter.statusCode = static_cast<pjsip_status_code>(statusCode);
    return POLPHONE_PJ_TRY(pj::Call::answer(parameter));
}

util::Result<void> SipCall::hangupCall()
{
    pj::CallOpParam parameter(true);
    return POLPHONE_PJ_TRY(hangup(parameter));
}

bool SipCall::canReap() const noexcept
{
    return callbackDepth_.load() == 0U;
}

std::size_t SipCall::liveCount() noexcept
{
    return liveCount_.load();
}

SipCall::CallbackScope::CallbackScope(SipCall& call) noexcept
    : call_(call)
{
    ++call_.callbackDepth_;
}

SipCall::CallbackScope::~CallbackScope()
{
    --call_.callbackDepth_;
    call_.registry_.notifyCallbackComplete();
}

void SipCall::onCallState(pj::OnCallStateParam&) noexcept
{
    CallbackScope callback(*this);
    bool disconnected = false;
    try {
        const pj::CallInfo information = getInfo();
        disconnected = information.state == PJSIP_INV_STATE_DISCONNECTED;
        const auto state = callStateFromPjsip(information.state);
        state_.updateCall(app::CallSnapshot{
            state,
            static_cast<int>(information.lastStatusCode),
            information.lastReason,
            information.remoteUri});
        static_cast<void>(events_.push(app::UiEvent{
            information.lastStatusCode >= 400
                ? app::UiEventSeverity::Warning
                : app::UiEventSeverity::Info,
            "call",
            "Estado: " + callStateText(state) + " code="
                + std::to_string(information.lastStatusCode) + " reason="
                + information.lastReason}));
    } catch (const pj::Error& error) {
        publishCallbackFailure("onCallState", describe(error));
    } catch (const std::exception& error) {
        publishCallbackFailure("onCallState", error.what());
    } catch (...) {
        publishCallbackFailure("onCallState", "exceção desconhecida");
    }
    if (disconnected) registry_.retire(this);
}

void SipCall::onCallTsxState(pj::OnCallTsxStateParam& parameter) noexcept
{
    CallbackScope callback(*this);
    try {
        if (parameter.e.type != PJSIP_EVENT_TSX_STATE) return;
        const auto& transaction = parameter.e.body.tsxState.tsx;
        if (transaction.state != PJSIP_TSX_STATE_COMPLETED
            && transaction.state != PJSIP_TSX_STATE_TERMINATED) {
            return;
        }
        static_cast<void>(events_.push(app::UiEvent{
            transaction.statusCode >= 400
                ? app::UiEventSeverity::Warning
                : app::UiEventSeverity::Info,
            "call",
            "Transação " + transaction.method + " concluída: "
                + std::to_string(transaction.statusCode) + " "
                + transaction.statusText}));
    } catch (const pj::Error& error) {
        publishCallbackFailure("onCallTsxState", describe(error));
    } catch (const std::exception& error) {
        publishCallbackFailure("onCallTsxState", error.what());
    } catch (...) {
        publishCallbackFailure("onCallTsxState", "exceção desconhecida");
    }
}

void SipCall::publishCallbackFailure(
    std::string_view callback,
    std::string_view detail) noexcept
{
    try {
        static_cast<void>(logger_.error(
            "call",
            std::string(callback) + " falhou sem propagar exceção: " + std::string(detail)));
        static_cast<void>(events_.push(app::UiEvent{
            app::UiEventSeverity::Error,
            "call",
            std::string(callback) + " falhou; consulte o log técnico."}));
    } catch (...) {
        static_cast<void>(logger_.error(
            "call", "Callback falhou e o diagnóstico detalhado não pôde ser alocado."));
    }
}

} // namespace polphone::sip
