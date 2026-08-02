/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "sip/SipAccount.h"

#include "sip/CallRegistry.h"
#include "sip/PjErrors.h"
#include "sip/SipCall.h"

#include <pjsip/sip_msg.h>

#include <exception>
#include <string>
#include <utility>

namespace polphone::sip {
namespace {

app::UiEventSeverity eventSeverity(int code, pj_status_t status, bool active) noexcept
{
    if (active || (code >= 200 && code < 300)) return app::UiEventSeverity::Info;
    if (status != PJ_SUCCESS || code >= 400) return app::UiEventSeverity::Error;
    return app::UiEventSeverity::Warning;
}

app::RegistrationState registrationStateFor(
    const pj::OnRegStateParam& parameter,
    const pj::AccountInfo& information) noexcept
{
    if (information.regIsActive) return app::RegistrationState::Registered;
    if (parameter.code >= 200 && parameter.code < 300 && parameter.expiration == 0U) {
        return app::RegistrationState::Unregistered;
    }
    if (parameter.status != PJ_SUCCESS || parameter.code >= 400) {
        return app::RegistrationState::Failed;
    }
    return app::RegistrationState::Registering;
}

} // namespace

std::string registrationStatusMessage(
    int sipCode,
    pj_status_t transportStatus,
    std::string_view reason)
{
    if (transportStatus == PJ_ETIMEDOUT || sipCode == PJSIP_SC_REQUEST_TIMEOUT) {
        return "Sem resposta do registrar — firewall/NAT pode estar bloqueando UDP.";
    }
    switch (sipCode) {
    case PJSIP_SC_UNAUTHORIZED:
    case PJSIP_SC_PROXY_AUTHENTICATION_REQUIRED:
        return "Autenticação recusada — verifique usuário, senha e realm.";
    case PJSIP_SC_FORBIDDEN:
        return "Registro proibido — a conta pode estar bloqueada ou o IP não é permitido.";
    case PJSIP_SC_NOT_FOUND:
        return "Ramal ou registrar não encontrado — verifique idUri e registrarUri.";
    case PJSIP_SC_SERVICE_UNAVAILABLE:
        return "PABX indisponível — o registro será repetido automaticamente.";
    default:
        break;
    }
    if (sipCode >= 200 && sipCode < 300) {
        return "Registro SIP aceito.";
    }
    if (transportStatus != PJ_SUCCESS) {
        return "Falha de transporte durante o registro — verifique rede, DNS e firewall UDP.";
    }
    if (!reason.empty()) return "REGISTER: " + std::to_string(sipCode) + " " + std::string(reason);
    return "Estado de registro atualizado (SIP " + std::to_string(sipCode) + ").";
}

SipAccount::SipAccount(app::AppState& state,
                       app::EventQueue& events,
                       logging::Logger& logger,
                       CallRegistry& calls) noexcept
    : state_(state), events_(events), logger_(logger), calls_(calls)
{
}

SipAccount::~SipAccount()
{
    shutdownAccount();
}

util::Result<void> SipAccount::createFrom(const config::SipConfig& config)
{
    if (created_) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "A conta SIP já foi criada; reinicie a aplicação para trocar a identidade.");
    }

    pj::AccountConfig accountConfig;
    accountConfig.idUri = config.idUri;
    accountConfig.regConfig.registrarUri = config.registrarUri;
    accountConfig.regConfig.registerOnAdd = config.registerOnStartup;
    accountConfig.regConfig.timeoutSec = static_cast<unsigned>(config.regTimeoutSec);
    accountConfig.regConfig.retryIntervalSec =
        static_cast<unsigned>(config.regRetryIntervalSec);
    accountConfig.sipConfig.authCreds.emplace_back(
        "digest", config.realm, config.username, 0, config.password);
    if (!config.proxyUri.empty()) accountConfig.sipConfig.proxies.push_back(config.proxyUri);

    state_.updateRegistration(app::RegistrationSnapshot{
        config.registerOnStartup
            ? app::RegistrationState::Registering
            : app::RegistrationState::Unregistered,
        false,
        0,
        0,
        {},
        config.registerOnStartup ? "Registro solicitado." : "Registro automático desabilitado."});

    const auto created = POLPHONE_PJ_TRY(pj::Account::create(accountConfig, true));
    if (!created) return created;
    created_ = true;
    static_cast<void>(logger_.info(
        "sip",
        config.registerOnStartup
            ? "Conta SIP criada; REGISTER inicial solicitado."
            : "Conta SIP criada sem registro automático."));
    return util::Result<void>::success();
}

util::Result<void> SipAccount::setRegistrationEnabled(bool enabled)
{
    if (!created_ || !isValid()) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "A conta SIP não está disponível; revise a inicialização antes de alterar o registro.");
    }
    bool registrationActive = false;
    {
        std::lock_guard<std::mutex> lock(registrationMutex_);
        registrationActive = registrationActive_;
    }
    state_.updateRegistration(app::RegistrationSnapshot{
        enabled ? app::RegistrationState::Registering : app::RegistrationState::Unregistering,
        registrationActive,
        0,
        0,
        {},
        enabled ? "Registro solicitado." : "Cancelamento do registro solicitado."});
    return POLPHONE_PJ_TRY(setRegistration(enabled));
}

util::Result<void> SipAccount::unregisterAndWait(std::chrono::milliseconds timeout)
{
    if (!created_ || !isValid()) return util::Result<void>::success();

    const auto information = POLPHONE_PJ_TRY(getInfo());
    if (!information) return util::Result<void>::failure(information.error());
    if (!information.value().regIsConfigured || !information.value().regIsActive) {
        return util::Result<void>::success();
    }
    const auto requested = setRegistrationEnabled(false);
    if (!requested) return requested;

    std::unique_lock<std::mutex> lock(registrationMutex_);
    if (!registrationCondition_.wait_for(
            lock, timeout, [this] { return !registrationActive_; })) {
        static_cast<void>(logger_.warning(
            "sip", "Timeout de 3 s ao aguardar un-REGISTER; prosseguindo com shutdown."));
    }
    return util::Result<void>::success();
}

void SipAccount::shutdownAccount() noexcept
{
    if (shuttingDown_.exchange(true)) return;
    if (created_) {
        const auto unregistered = unregisterAndWait();
        if (!unregistered) {
            static_cast<void>(logger_.warning(
                "sip", unregistered.error().message + " " + unregistered.error().detail));
        }
    }
    if (created_ && isValid()) pj::Account::shutdown();
    created_ = false;
}

void SipAccount::onRegState(pj::OnRegStateParam& parameter) noexcept
{
    try {
        const pj::AccountInfo information = getInfo();
        publishRegistration(parameter, information);
    } catch (const pj::Error& error) {
        publishCallbackFailure("onRegState", describe(error));
    } catch (const std::exception& error) {
        publishCallbackFailure("onRegState", error.what());
    } catch (...) {
        publishCallbackFailure("onRegState", "exceção desconhecida");
    }
}

void SipAccount::onIncomingCall(pj::OnIncomingCallParam& parameter) noexcept
{
    try {
        if (shuttingDown_.load() || calls_.hasActiveCall()) {
            pj::Call busyCall(*this, parameter.callId);
            pj::CallOpParam busyResponse(true);
            busyResponse.statusCode = PJSIP_SC_BUSY_HERE;
            busyCall.answer(busyResponse);
            static_cast<void>(events_.push(app::UiEvent{
                app::UiEventSeverity::Warning,
                "call",
                "Chamada entrante recusada com 486 Busy Here: já existe chamada ativa."}));
            return;
        }

        auto call = std::make_unique<SipCall>(
            *this, calls_, state_, events_, logger_, parameter.callId);
        SipCall* adoptedCall = call.get();
        const auto adopted = calls_.adopt(call);
        if (!adopted) {
            static_cast<void>(call->answer(PJSIP_SC_BUSY_HERE));
            return;
        }
        const auto ringing = adoptedCall->answer(PJSIP_SC_RINGING);
        if (!ringing) {
            calls_.retire(adoptedCall);
            publishCallbackFailure("onIncomingCall", ringing.error().detail);
            return;
        }
        static_cast<void>(events_.push(app::UiEvent{
            app::UiEventSeverity::Info,
            "call",
            "Chamada entrante aguardando atendimento (180 Ringing)."}));
    } catch (const pj::Error& error) {
        publishCallbackFailure("onIncomingCall", describe(error));
    } catch (const std::exception& error) {
        publishCallbackFailure("onIncomingCall", error.what());
    } catch (...) {
        publishCallbackFailure("onIncomingCall", "exceção desconhecida");
    }
}

void SipAccount::publishRegistration(
    const pj::OnRegStateParam& parameter,
    const pj::AccountInfo& information)
{
    std::string message = registrationStatusMessage(
        static_cast<int>(parameter.code), parameter.status, parameter.reason);
    if (!information.regIsActive && parameter.code >= 200 && parameter.code < 300
        && parameter.expiration == 0U) {
        message = "Registro SIP removido.";
    }
    const unsigned expiresSec = information.regIsActive ? information.regExpiresSec : 0U;
    const auto registrationState = registrationStateFor(parameter, information);
    state_.updateRegistration(app::RegistrationSnapshot{
        registrationState,
        information.regIsActive,
        static_cast<int>(parameter.code),
        expiresSec,
        parameter.reason,
        message});
    {
        std::lock_guard<std::mutex> lock(registrationMutex_);
        registrationActive_ = information.regIsActive;
    }
    registrationCondition_.notify_all();

    static_cast<void>(events_.push(app::UiEvent{
        eventSeverity(static_cast<int>(parameter.code), parameter.status, information.regIsActive),
        "sip",
        message + " code=" + std::to_string(parameter.code)
            + " expires=" + std::to_string(expiresSec) + "s"}));
}

void SipAccount::publishCallbackFailure(
    std::string_view callback,
    std::string_view detail) noexcept
{
    try {
        static_cast<void>(logger_.error(
            "sip",
            std::string(callback) + " falhou sem propagar exceção: " + std::string(detail)));
        static_cast<void>(events_.push(app::UiEvent{
            app::UiEventSeverity::Error,
            "sip",
            std::string(callback) + " falhou; consulte o log técnico."}));
    } catch (...) {
        static_cast<void>(logger_.error(
            "sip", "Callback falhou e o diagnóstico detalhado não pôde ser alocado."));
    }
}

} // namespace polphone::sip
