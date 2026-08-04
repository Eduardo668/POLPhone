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

std::string registrarTarget(std::string_view registrarUri)
{
    const std::string normalized = util::trim(registrarUri);
    if (!startsWithSipScheme(normalized)) return {};

    const bool secure = normalized.size() >= 5U
        && static_cast<char>(std::tolower(
               static_cast<unsigned char>(normalized[3]))) == 's';
    const std::size_t schemeLength = secure ? 5U : 4U;
    const std::size_t authorityEnd = normalized.find_first_of(";?", schemeLength);
    std::string authority = normalized.substr(
        schemeLength,
        authorityEnd == std::string::npos
            ? std::string::npos
            : authorityEnd - schemeLength);
    const std::size_t userInfoEnd = authority.rfind('@');
    if (userInfoEnd != std::string::npos) authority.erase(0U, userInfoEnd + 1U);
    if (authority.empty()
        || std::any_of(authority.cbegin(), authority.cend(), [](char value) {
               return std::isspace(static_cast<unsigned char>(value)) != 0;
           })) {
        return {};
    }
    return std::string(secure ? "sips:" : "sip:") + authority;
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
    std::string_view domain,
    std::string_view registrarUri)
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
    const std::string registrar = registrarTarget(registrarUri);
    if (!registrar.empty()) {
        const std::size_t schemeEnd = registrar.find(':');
        return util::Result<std::string>::success(
            registrar.substr(0U, schemeEnd + 1U) + normalized + "@"
            + registrar.substr(schemeEnd + 1U));
    }
    return util::Result<std::string>::success("sip:" + normalized + "@" + normalizedDomain);
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

std::string_view callMediaStatusName(pjsua_call_media_status status) noexcept
{
    switch (status) {
    case PJSUA_CALL_MEDIA_NONE: return "NONE";
    case PJSUA_CALL_MEDIA_ACTIVE: return "ACTIVE";
    case PJSUA_CALL_MEDIA_LOCAL_HOLD: return "LOCAL_HOLD";
    case PJSUA_CALL_MEDIA_REMOTE_HOLD: return "REMOTE_HOLD";
    case PJSUA_CALL_MEDIA_ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

std::string callStatusMessage(int sipCode, std::string_view reason)
{
    switch (sipCode) {
    case PJSIP_SC_NOT_FOUND:
        return "Destino SIP não encontrado — verifique o número, domínio e rota do PABX.";
    case PJSIP_SC_REQUEST_TIMEOUT:
        return "Sem resposta ao INVITE — verifique conectividade, NAT e firewall UDP.";
    case PJSIP_SC_BUSY_HERE:
        return "Destino ocupado — aguarde e tente novamente.";
    case PJSIP_SC_DECLINE:
        return "Chamada recusada pelo destino.";
    case PJSIP_SC_SERVICE_UNAVAILABLE:
        return "PABX ou rota de saída indisponível — tente novamente mais tarde.";
    default:
        break;
    }
    if (!reason.empty()) {
        return "Chamada SIP: " + std::to_string(sipCode) + " " + std::string(reason);
    }
    return "Estado da chamada atualizado (SIP " + std::to_string(sipCode) + ").";
}

SipCall::SipCall(pj::Account& account,
                 CallRegistry& registry,
                 app::AppState& state,
                 app::EventQueue& events,
                 logging::Logger& logger,
                 app::CallDirection direction,
                 int callId)
    : pj::Call(account, callId),
      registry_(registry),
      state_(state),
      events_(events),
      logger_(logger),
      direction_(direction)
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
    failPendingInfo(PJSIP_SC_CALL_TSX_DOES_NOT_EXIST, "Chamada destruída");
    disconnectAudio(false, false);
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
        app::CallState::Calling, direction_, 0, {}, destinationUri_});
    return util::Result<void>::success();
}

util::Result<void> SipCall::announceIncoming()
{
    const auto information = POLPHONE_PJ_TRY(getInfo());
    if (!information) return util::Result<void>::failure(information.error());
    destinationUri_ = information.value().remoteUri;
    state_.updateCall(app::CallSnapshot{
        app::CallState::Incoming,
        direction_,
        static_cast<int>(information.value().lastStatusCode),
        information.value().lastReason,
        destinationUri_});
    return util::Result<void>::success();
}

util::Result<void> SipCall::answer(int statusCode)
{
    const bool finalIncomingResponse = direction_ == app::CallDirection::Incoming
        && statusCode >= PJSIP_SC_OK;
    if (finalIncomingResponse) {
        int expected = 0;
        if (!incomingFinalResponse_.compare_exchange_strong(expected, statusCode)) {
            if (expected == statusCode) return util::Result<void>::success();
            return util::Result<void>::failure(
                util::ErrorCode::Runtime,
                "A chamada recebida já teve uma resposta local final.",
                "SIP " + std::to_string(expected));
        }
    }
    pj::CallOpParam parameter(true);
    parameter.statusCode = static_cast<pjsip_status_code>(statusCode);
    auto answered = POLPHONE_PJ_TRY(pj::Call::answer(parameter));
    if (!answered && finalIncomingResponse) {
        int expected = statusCode;
        static_cast<void>(incomingFinalResponse_.compare_exchange_strong(expected, 0));
    }
    return answered;
}

util::Result<void> SipCall::hangupCall()
{
    if (hangupRequested_.exchange(true)) return util::Result<void>::success();
    pj::CallOpParam parameter(true);
    auto hungUp = POLPHONE_PJ_TRY(hangup(parameter));
    if (!hungUp) hangupRequested_ = false;
    return hungUp;
}

util::Result<DtmfInfoResponse> SipCall::sendDtmfInfo(
    char digit,
    unsigned durationMs,
    std::string correlationId,
    std::chrono::milliseconds timeout)
{
    std::shared_ptr<PendingInfo> exchange;
    try {
        exchange = std::make_shared<PendingInfo>();
        exchange->correlationId = std::move(correlationId);
        {
            std::lock_guard<std::mutex> lock(infoMutex_);
            if (pendingInfo_ != nullptr) {
                return util::Result<DtmfInfoResponse>::failure(
                    util::ErrorCode::Runtime,
                    "Já existe uma transação SIP INFO aguardando resposta.",
                    pendingInfo_->correlationId);
            }
            pendingInfo_ = exchange;
            unboundInfo_.push_back(exchange);
        }

        pj::CallSendDtmfParam parameter;
        parameter.method = PJSUA_DTMF_METHOD_SIP_INFO;
        parameter.duration = durationMs;
        parameter.digits.assign(1U, digit);
        try {
            sendDtmf(parameter);
        } catch (const pj::Error& error) {
            std::lock_guard<std::mutex> lock(infoMutex_);
            unboundInfo_.erase(
                std::remove(unboundInfo_.begin(), unboundInfo_.end(), exchange),
                unboundInfo_.end());
            if (pendingInfo_ == exchange) pendingInfo_.reset();
            return util::Result<DtmfInfoResponse>::failure(
                makePjError(error, "Call::sendDtmf SIP INFO"));
        }

        std::unique_lock<std::mutex> lock(infoMutex_);
        const bool completed = infoCondition_.wait_for(
            lock, timeout, [&exchange] { return exchange->completed; });
        if (!completed) {
            exchange->timedOut = true;
            exchange->completed = true;
            exchange->statusCode = PJSIP_SC_REQUEST_TIMEOUT;
            exchange->statusText = "Request Timeout";
            unboundInfo_.erase(
                std::remove(unboundInfo_.begin(), unboundInfo_.end(), exchange),
                unboundInfo_.end());
        }
        if (pendingInfo_ == exchange) pendingInfo_.reset();
        return util::Result<DtmfInfoResponse>::success(DtmfInfoResponse{
            exchange->statusCode, exchange->statusText, exchange->timedOut});
    } catch (const std::exception& error) {
        try {
            std::lock_guard<std::mutex> lock(infoMutex_);
            unboundInfo_.erase(
                std::remove(unboundInfo_.begin(), unboundInfo_.end(), exchange),
                unboundInfo_.end());
            if (pendingInfo_ == exchange) pendingInfo_.reset();
        } catch (...) {
        }
        return util::Result<DtmfInfoResponse>::failure(
            util::ErrorCode::Runtime,
            "Falha ao aguardar a resposta do SIP INFO.",
            error.what());
    } catch (...) {
        try {
            std::lock_guard<std::mutex> lock(infoMutex_);
            unboundInfo_.erase(
                std::remove(unboundInfo_.begin(), unboundInfo_.end(), exchange),
                unboundInfo_.end());
            if (pendingInfo_ == exchange) pendingInfo_.reset();
        } catch (...) {
        }
        return util::Result<DtmfInfoResponse>::failure(
            util::ErrorCode::Runtime,
            "Falha desconhecida ao aguardar a resposta do SIP INFO.");
    }
}

std::optional<pj::AudioMedia> SipCall::audioMedia() const
{
    std::lock_guard<std::mutex> lock(mediaMutex_);
    return audioMedia_;
}

int SipCall::audioConfSlot() const noexcept
{
    return audioConfSlot_.load();
}

bool SipCall::hasActiveAudio() const noexcept
{
    return audioConnected_.load();
}

util::Result<void> SipCall::setMuted(bool muted)
{
    if (!audioConnected_.load()) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "O áudio da chamada ainda não está ativo.");
    }
    if (muted_.load() == muted) return util::Result<void>::success();
    try {
        std::optional<pj::AudioMedia> media;
        {
            std::lock_guard<std::mutex> lock(mediaMutex_);
            media = audioMedia_;
        }
        if (!media.has_value()) {
            return util::Result<void>::failure(
                util::ErrorCode::Runtime,
                "A mídia de áudio da chamada não está disponível.");
        }
        pj::AudioMedia& capture = pj::Endpoint::instance()
            .audDevManager().getCaptureDevMedia();
        if (muted) capture.stopTransmit(*media);
        else capture.startTransmit(*media);
        {
            std::lock_guard<std::mutex> lock(mediaMutex_);
            captureConnected_ = !muted;
        }
        muted_ = muted;
        static_cast<void>(events_.push(app::UiEvent{
            app::UiEventSeverity::Info,
            "media",
            muted ? "Microfone silenciado." : "Microfone reativado."}));
        return util::Result<void>::success();
    } catch (const pj::Error& error) {
        return util::Result<void>::failure(makePjError(error, "alterar mudo da chamada"));
    }
}

bool SipCall::isMuted() const noexcept
{
    return muted_.load();
}

app::CallDirection SipCall::direction() const noexcept { return direction_; }

bool SipCall::canReap() const noexcept
{
    return callbackDepth_.load() == 0U && externalUseDepth_.load() == 0U;
}

void SipCall::retainExternalUse() noexcept
{
    ++externalUseDepth_;
}

void SipCall::releaseExternalUse() noexcept
{
    unsigned current = externalUseDepth_.load();
    while (current != 0U
           && !externalUseDepth_.compare_exchange_weak(current, current - 1U)) {
    }
    registry_.notifyCallbackComplete();
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
        if (disconnected) {
            failPendingInfo(
                PJSIP_SC_CALL_TSX_DOES_NOT_EXIST,
                "Call/Transaction Does Not Exist");
            // O PJSUA remove o conference port ao destruir o stream. Neste
            // ponto só limpamos nosso bookkeeping para não desconectar uma
            // porta que já deixou de existir.
            disconnectAudio(false, false);
        }
        const auto state = callStateFromPjsip(information.state);
        state_.updateCall(app::CallSnapshot{
            state,
            direction_,
            static_cast<int>(information.lastStatusCode),
            information.lastReason,
            information.remoteUri});
        static_cast<void>(events_.push(app::UiEvent{
            information.lastStatusCode >= 400
                ? app::UiEventSeverity::Warning
                : app::UiEventSeverity::Info,
            "call",
            information.lastStatusCode >= 400
                ? callStatusMessage(
                    static_cast<int>(information.lastStatusCode), information.lastReason)
                : "Estado: " + callStateText(state) + " code="
                    + std::to_string(information.lastStatusCode) + " reason="
                    + information.lastReason}));
    } catch (const pj::Error& error) {
        publishPjCallbackFailure("onCallState", error);
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
        if (transaction.role == PJSIP_ROLE_UAC && transaction.method == "INFO") {
            handleInfoTransaction(transaction);
            return;
        }
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
        publishPjCallbackFailure("onCallTsxState", error);
    } catch (const std::exception& error) {
        publishCallbackFailure("onCallTsxState", error.what());
    } catch (...) {
        publishCallbackFailure("onCallTsxState", "exceção desconhecida");
    }
}

void SipCall::handleInfoTransaction(
    const pj::SipTransaction& transaction) noexcept
{
    try {
        const bool terminal = transaction.state == PJSIP_TSX_STATE_COMPLETED
            || transaction.state == PJSIP_TSX_STATE_TERMINATED;
        std::shared_ptr<PendingInfo> exchange;
        bool completedNow = false;
        {
            std::lock_guard<std::mutex> lock(infoMutex_);
            const auto found = infoTransactions_.find(transaction.pjTransaction);
            if (found != infoTransactions_.end()) {
                exchange = found->second;
            } else if (!unboundInfo_.empty()) {
                exchange = unboundInfo_.front();
                unboundInfo_.pop_front();
                exchange->transaction = transaction.pjTransaction;
                infoTransactions_.emplace(transaction.pjTransaction, exchange);
            }
            if (exchange != nullptr && terminal && !exchange->completed) {
                exchange->statusCode = transaction.statusCode == 0
                    ? PJSIP_SC_REQUEST_TIMEOUT
                    : transaction.statusCode;
                exchange->statusText = transaction.statusText.empty()
                    ? "Sem resposta SIP"
                    : transaction.statusText;
                exchange->timedOut = exchange->statusCode == PJSIP_SC_REQUEST_TIMEOUT;
                exchange->completed = true;
                completedNow = true;
            }
            if (transaction.state == PJSIP_TSX_STATE_TERMINATED) {
                infoTransactions_.erase(transaction.pjTransaction);
            }
        }
        if (!completedNow || exchange == nullptr) return;

        infoCondition_.notify_all();
        const bool accepted = exchange->statusCode >= 200
            && exchange->statusCode < 300;
        static_cast<void>(logger_.log(
            accepted ? logging::LogLevel::Info : logging::LogLevel::Warning,
            "dtmf",
            "SIP INFO respondido: " + std::to_string(exchange->statusCode)
                + " " + exchange->statusText,
            {},
            exchange->correlationId));
        static_cast<void>(events_.push(app::UiEvent{
            accepted ? app::UiEventSeverity::Info : app::UiEventSeverity::Warning,
            "dtmf",
            "id=" + exchange->correlationId + " INFO: "
                + std::to_string(exchange->statusCode) + " "
                + exchange->statusText}));
    } catch (const pj::Error& error) {
        publishPjCallbackFailure("onCallTsxState/INFO", error);
    } catch (const std::exception& error) {
        publishCallbackFailure("onCallTsxState/INFO", error.what());
    } catch (...) {
        publishCallbackFailure("onCallTsxState/INFO", "exceção desconhecida");
    }
}

void SipCall::failPendingInfo(
    int statusCode,
    std::string_view statusText) noexcept
{
    try {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(infoMutex_);
            if (pendingInfo_ != nullptr && !pendingInfo_->completed) {
                pendingInfo_->statusCode = statusCode;
                pendingInfo_->statusText = std::string(statusText);
                pendingInfo_->completed = true;
                unboundInfo_.erase(
                    std::remove(
                        unboundInfo_.begin(), unboundInfo_.end(), pendingInfo_),
                    unboundInfo_.end());
                changed = true;
            }
        }
        if (changed) infoCondition_.notify_all();
    } catch (...) {
    }
}

void SipCall::onCallMediaState(pj::OnCallMediaStateParam&) noexcept
{
    CallbackScope callback(*this);
    try {
        const pj::CallInfo information = getInfo();
        bool foundAudio = false;
        for (const auto& media : information.media) {
            if (media.type != PJMEDIA_TYPE_AUDIO) continue;
            foundAudio = true;
            static_cast<void>(logger_.info(
                "media",
                "Mídia de áudio index=" + std::to_string(media.index)
                    + " status=" + std::string(callMediaStatusName(media.status))
                    + " slot=" + std::to_string(media.audioConfSlot) + "."));

            switch (media.status) {
            case PJSUA_CALL_MEDIA_ACTIVE: {
                const auto connected = connectAudio(media.index);
                if (!connected) {
                    publishMediaFailure(connected.error().message, connected.error().detail);
                }
                break;
            }
            case PJSUA_CALL_MEDIA_LOCAL_HOLD:
            case PJSUA_CALL_MEDIA_REMOTE_HOLD: {
                disconnectAudio(true);
                const auto heldMedia = POLPHONE_PJ_TRY(getAudioMedia(media.index));
                if (heldMedia) {
                    std::lock_guard<std::mutex> lock(mediaMutex_);
                    audioMedia_ = heldMedia.value();
                    audioConfSlot_ = heldMedia.value().getPortId();
                } else {
                    publishMediaFailure(
                        "Não foi possível preservar a mídia durante hold.",
                        heldMedia.error().detail);
                }
                break;
            }
            case PJSUA_CALL_MEDIA_ERROR:
                disconnectAudio(false, false);
                publishMediaFailure(
                    "A negociação de mídia falhou; a chamada continuará sinalizada.",
                    "mediaIndex=" + std::to_string(media.index));
                break;
            case PJSUA_CALL_MEDIA_NONE:
                disconnectAudio(false, false);
                break;
            }
        }
        if (!foundAudio) {
            disconnectAudio(false, false);
            static_cast<void>(logger_.info(
                "media", "A chamada não possui fluxo de áudio negociado."));
        }
    } catch (const pj::Error& error) {
        publishPjCallbackFailure("onCallMediaState", error);
    } catch (const std::exception& error) {
        publishCallbackFailure("onCallMediaState", error.what());
    } catch (...) {
        publishCallbackFailure("onCallMediaState", "exceção desconhecida");
    }
}

util::Result<void> SipCall::connectAudio(unsigned mediaIndex)
{
    try {
        pj::AudioMedia callMedia = getAudioMedia(static_cast<int>(mediaIndex));
        const int callSlot = callMedia.getPortId();
        {
            std::lock_guard<std::mutex> lock(mediaMutex_);
            if (audioConnected_.load() && audioConfSlot_.load() == callSlot
                && audioMedia_.has_value()) {
                return util::Result<void>::success();
            }
        }
        disconnectAudio();
        pj::AudDevManager& manager = pj::Endpoint::instance().audDevManager();
        pj::AudioMedia& playback = manager.getPlaybackDevMedia();
        pj::AudioMedia& capture = manager.getCaptureDevMedia();

        {
            std::lock_guard<std::mutex> lock(mediaMutex_);
            audioMedia_ = callMedia;
            audioConfSlot_ = callSlot;
            activeMediaIndex_ = mediaIndex;
        }
        callMedia.startTransmit(playback);
        {
            std::lock_guard<std::mutex> lock(mediaMutex_);
            playbackConnected_ = true;
        }
        capture.startTransmit(callMedia);
        {
            std::lock_guard<std::mutex> lock(mediaMutex_);
            captureConnected_ = true;
        }
        audioConnected_ = true;
        muted_ = false;

        try {
            const pj::StreamInfo stream = getStreamInfo(mediaIndex);
            const pj::MediaTransportInfo transport = getMedTransportInfo(mediaIndex);
            {
                std::lock_guard<std::mutex> lock(mediaMutex_);
                diagnostics_.codec = stream.codecName + "/"
                    + std::to_string(stream.codecClockRate);
                diagnostics_.localRtp = transport.localRtpName;
                diagnostics_.remoteRtp = stream.remoteRtpAddress.empty()
                    ? transport.srcRtpName : stream.remoteRtpAddress;
            }
            static_cast<void>(logger_.info(
                "media",
                "Áudio bidirecional conectado: codec=" + stream.codecName + "/"
                    + std::to_string(stream.codecClockRate)
                    + " localRtp=" + transport.localRtpName
                    + " remoteRtp=" + stream.remoteRtpAddress
                    + " slot=" + std::to_string(callMedia.getPortId()) + "."));
            static_cast<void>(events_.push(app::UiEvent{
                app::UiEventSeverity::Info,
                "media",
                "Áudio ativo: " + stream.codecName + "/"
                    + std::to_string(stream.codecClockRate) + "."}));
        } catch (const pj::Error& error) {
            static_cast<void>(logger_.warning(
                "media",
                "Áudio conectado, mas os detalhes de codec/RTP não puderam ser lidos: "
                    + describe(error)));
        } catch (...) {
            static_cast<void>(logger_.warning(
                "media", "Áudio conectado, mas o diagnóstico de codec/RTP falhou."));
        }
        return util::Result<void>::success();
    } catch (const pj::Error& error) {
        disconnectAudio();
        return util::Result<void>::failure(makePjError(error, "conectar áudio da chamada"));
    } catch (const std::exception& error) {
        disconnectAudio();
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Falha inesperada ao conectar o áudio da chamada.",
            error.what());
    } catch (...) {
        disconnectAudio();
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Falha desconhecida ao conectar o áudio da chamada.");
    }
}

void SipCall::disconnectAudio(bool retainMedia, bool stopTransmissions) noexcept
{
    try {
        std::optional<pj::AudioMedia> media;
        bool stopPlayback = false;
        bool stopCapture = false;
        {
            std::lock_guard<std::mutex> lock(mediaMutex_);
            media = audioMedia_;
            stopPlayback = playbackConnected_;
            stopCapture = captureConnected_;
            playbackConnected_ = false;
            captureConnected_ = false;
            audioConnected_ = false;
            muted_ = false;
            if (!retainMedia) {
                audioMedia_.reset();
                audioConfSlot_ = PJSUA_INVALID_ID;
                diagnostics_ = CallDiagnostics{};
            }
        }
        if (stopTransmissions && media.has_value()) {
            pj::AudDevManager& manager = pj::Endpoint::instance().audDevManager();
            if (stopPlayback) {
                try {
                    media->stopTransmit(manager.getPlaybackDevMedia());
                } catch (const pj::Error& error) {
                    static_cast<void>(logger_.warning(
                        "media", "Falha ao desconectar reprodução: " + describe(error)));
                }
            }
            if (stopCapture) {
                try {
                    manager.getCaptureDevMedia().stopTransmit(*media);
                } catch (const pj::Error& error) {
                    static_cast<void>(logger_.warning(
                        "media", "Falha ao desconectar captura: " + describe(error)));
                }
            }
        }
    } catch (...) {
        audioConnected_ = false;
        muted_ = false;
        if (!retainMedia) audioConfSlot_ = PJSUA_INVALID_ID;
    }
}

CallDiagnostics SipCall::diagnostics() const
{
    try {
        CallDiagnostics result;
        unsigned mediaIndex = 0U;
        {
            std::lock_guard<std::mutex> lock(mediaMutex_);
            result = diagnostics_;
            mediaIndex = activeMediaIndex_;
        }
        if (!audioConnected_.load()) return result;
        const pj::StreamStat statistics = getStreamStat(mediaIndex);
        result.packetsSent = statistics.rtcp.txStat.pkt;
        result.packetsReceived = statistics.rtcp.rxStat.pkt;
        result.packetsLost = statistics.rtcp.rxStat.loss;
        result.jitterMs = static_cast<double>(statistics.rtcp.rxStat.jitterUsec.mean) / 1000.0;
        result.hasRtpStatistics = true;
        return result;
    } catch (...) {
        std::lock_guard<std::mutex> lock(mediaMutex_);
        return diagnostics_;
    }
}

void SipCall::publishMediaFailure(
    std::string_view message,
    std::string_view detail) noexcept
{
    try {
        static_cast<void>(logger_.error(
            "media", std::string(message) + " " + std::string(detail)));
        static_cast<void>(events_.push(app::UiEvent{
            app::UiEventSeverity::Error,
            "media",
            std::string(message)}));
    } catch (...) {
        static_cast<void>(logger_.error(
            "media", "Falha de mídia sem memória para diagnóstico detalhado."));
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

void SipCall::publishPjCallbackFailure(
    std::string_view callback,
    const pj::Error& error) noexcept
{
    try {
        const std::string detail = describe(error);
        publishCallbackFailure(callback, detail);
    } catch (...) {
        publishCallbackFailure(callback, "pj::Error sem memória para diagnóstico");
    }
}

} // namespace polphone::sip
