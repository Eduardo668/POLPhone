/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "dtmf/DtmfSender.h"

#include "sip/CallRegistry.h"
#include "sip/PjErrors.h"
#include "sip/SipCall.h"
#include "util/Time.h"

#include <pjmedia/errno.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>

namespace polphone::dtmf {
namespace {

util::Result<void> unsupportedMethod(DtmfMethod method)
{
    return util::Result<void>::failure(
        util::ErrorCode::Runtime,
        "O método DTMF solicitado ainda não está implementado nesta etapa.",
        std::string(methodName(method)));
}

} // namespace

DtmfSender::DtmfSender(
    sip::CallRegistry& calls,
    app::AppState& state,
    app::EventQueue& events,
    logging::Logger& logger) noexcept
    : calls_(calls), state_(state), events_(events), logger_(logger)
{
}

util::Result<void> DtmfSender::configure(const config::DtmfConfig& config)
{
    const auto method = parseMethod(config.defaultMethod);
    if (!method.has_value()) {
        return util::Result<void>::failure(
            util::ErrorCode::Validation,
            "O método DTMF padrão é inválido.",
            config.defaultMethod);
    }
    const auto timing = DtmfPlan::build("5", static_cast<unsigned>(config.durationMs),
                                        static_cast<unsigned>(config.gapMs));
    if (!timing) return util::Result<void>::failure(timing.error());
    if (config.volumeDbm0 < -30 || config.volumeDbm0 > 0) {
        return util::Result<void>::failure(
            util::ErrorCode::Validation,
            "O volume DTMF deve estar entre -30 e 0 dBm0.");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = DtmfSettings{
        *method,
        static_cast<unsigned>(config.durationMs),
        static_cast<unsigned>(config.gapMs),
        config.volumeDbm0,
        config.localFeedback,
        config.logDigits};
    return util::Result<void>::success();
}

DtmfSettings DtmfSender::settings() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_;
}

bool DtmfSender::inFlight() const noexcept
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        return inFlight_;
    } catch (...) {
        return true;
    }
}

util::Result<void> DtmfSender::begin(std::string correlationId)
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (inFlight_) {
            return util::Result<void>::failure(
                util::ErrorCode::Runtime,
                "Já existe um envio DTMF em andamento; aguarde sua conclusão.",
                currentCorrelationId_);
        }
        currentCorrelationId_ = std::move(correlationId);
        inFlight_ = true;
        return util::Result<void>::success();
    } catch (const std::exception& error) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Não foi possível iniciar o envio DTMF.",
            error.what());
    } catch (...) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Falha desconhecida ao iniciar o envio DTMF.");
    }
}

void DtmfSender::finish() noexcept
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        inFlight_ = false;
        currentCorrelationId_.clear();
    } catch (...) {
    }
}

DtmfSender::InFlightGuard::InFlightGuard(DtmfSender& sender) noexcept
    : sender_(sender)
{
}

DtmfSender::InFlightGuard::~InFlightGuard()
{
    sender_.finish();
}

util::Result<sip::SipCall*> DtmfSender::activeCall() const
{
    sip::SipCall* call = calls_.current();
    if (call == nullptr) {
        return util::Result<sip::SipCall*>::failure(
            util::ErrorCode::Runtime,
            "Sem chamada ativa para enviar DTMF.");
    }
    const auto snapshot = state_.call();
    if (snapshot.state != app::CallState::Confirmed) {
        return util::Result<sip::SipCall*>::failure(
            util::ErrorCode::Runtime,
            "A chamada ainda não está estabelecida; aguarde o estado CONFIRMED.",
            std::string(app::callStateName(snapshot.state)));
    }
    if (!call->hasActiveAudio()) {
        return util::Result<sip::SipCall*>::failure(
            util::ErrorCode::Runtime,
            "A mídia de áudio não está ativa; o DTMF não foi enviado.");
    }
    const auto information = POLPHONE_PJ_TRY(call->getInfo());
    if (!information) {
        return util::Result<sip::SipCall*>::failure(information.error());
    }
    if (information.value().state != PJSIP_INV_STATE_CONFIRMED) {
        return util::Result<sip::SipCall*>::failure(
            util::ErrorCode::Runtime,
            "A sessão SIP não está confirmada; o DTMF não foi enviado.",
            information.value().stateText);
    }
    return util::Result<sip::SipCall*>::success(call);
}

util::Result<int> DtmfSender::telephoneEventPayloadType(sip::SipCall& call) const
{
    const auto information = POLPHONE_PJ_TRY(call.getInfo());
    if (!information) return util::Result<int>::failure(information.error());
    for (const auto& media : information.value().media) {
        if (media.type != PJMEDIA_TYPE_AUDIO
            || media.status != PJSUA_CALL_MEDIA_ACTIVE) {
            continue;
        }
        const auto stream = POLPHONE_PJ_TRY(call.getStreamInfo(media.index));
        if (!stream) return util::Result<int>::failure(stream.error());
        return util::Result<int>::success(stream.value().audTxEventPt);
    }
    return util::Result<int>::failure(
        util::ErrorCode::Runtime,
        "Nenhum fluxo de áudio ativo foi encontrado para verificar telephone-event.");
}

util::Result<void> DtmfSender::sendRfc4733(
    sip::SipCall& call,
    char digit,
    unsigned durationMs)
{
    pj::CallSendDtmfParam parameter;
    parameter.method = PJSUA_DTMF_METHOD_RFC2833;
    parameter.duration = durationMs;
    parameter.digits.assign(1U, digit);
    try {
        call.sendDtmf(parameter);
        return util::Result<void>::success();
    } catch (const pj::Error& error) {
        if (error.status == PJMEDIA_RTP_EREMNORFC2833) {
            return util::Result<void>::failure(
                util::ErrorCode::Pjsip,
                "O outro lado não negociou telephone-event; RFC 4733 está indisponível nesta chamada.",
                sip::describe(error));
        }
        return util::Result<void>::failure(
            sip::makePjError(error, "Call::sendDtmf RFC4733"));
    }
}

std::string DtmfSender::nextCorrelationId()
{
    const unsigned long long value = sequence_.fetch_add(1U) + 1U;
    std::ostringstream output;
    output << "dtmf-" << std::setw(4) << std::setfill('0') << value;
    return output.str();
}

util::Result<DtmfResult> DtmfSender::send(const DtmfRequest& request)
{
    const auto plan = DtmfPlan::build(request.digits, request.durationMs, request.gapMs);
    if (!plan) return util::Result<DtmfResult>::failure(plan.error());
    if (request.method != DtmfMethod::Rfc4733) {
        const auto unsupported = unsupportedMethod(request.method);
        return util::Result<DtmfResult>::failure(unsupported.error());
    }

    std::string correlationId;
    try {
        correlationId = nextCorrelationId();
    } catch (const std::exception& error) {
        return util::Result<DtmfResult>::failure(
            util::ErrorCode::Runtime,
            "Não foi possível criar o identificador do envio DTMF.",
            error.what());
    }
    const auto started = begin(correlationId);
    if (!started) return util::Result<DtmfResult>::failure(started.error());
    InFlightGuard flight(*this);

    const auto active = activeCall();
    if (!active) return util::Result<DtmfResult>::failure(active.error());
    sip::SipCall* call = active.value();

    const auto payloadType = telephoneEventPayloadType(*call);
    if (!payloadType || payloadType.value() < 0) {
        static_cast<void>(logger_.log(
            logging::LogLevel::Warning,
            "dtmf",
            "telephone-event não aparece negociado; a tentativa RFC 4733 continuará para obter o erro real.",
            payloadType ? std::string_view{} : std::string_view(payloadType.error().detail),
            correlationId));
    } else {
        static_cast<void>(logger_.log(
            logging::LogLevel::Info,
            "dtmf",
            "telephone-event negociado para transmissão: PT="
                + std::to_string(payloadType.value()) + ".",
            {},
            correlationId));
    }

    std::size_t digitCount = 0U;
    for (const auto& step : plan.value()) {
        if (step.kind == DtmfPlanStep::Kind::Digit) ++digitCount;
    }
    DtmfResult result;
    result.correlationId = correlationId;
    try {
        result.perDigit.reserve(digitCount);
    } catch (const std::exception& error) {
        return util::Result<DtmfResult>::failure(
            util::ErrorCode::Runtime,
            "Não foi possível reservar o relatório DTMF.",
            error.what());
    }

    std::size_t digitIndex = 0U;
    for (const auto& step : plan.value()) {
        if (step.kind == DtmfPlanStep::Kind::Pause) {
            std::this_thread::sleep_for(std::chrono::milliseconds(step.pauseMs));
            continue;
        }
        const auto stillActive = activeCall();
        if (!stillActive || stillActive.value() != call) {
            return util::Result<DtmfResult>::failure(
                util::ErrorCode::Runtime,
                "A chamada terminou ou mudou durante o envio DTMF.");
        }
        ++digitIndex;
        static_cast<void>(logger_.log(
            logging::LogLevel::Info,
            "dtmf",
            "method=rfc4733 idx=" + std::to_string(digitIndex) + "/"
                + std::to_string(digitCount) + " digit=" + std::string(1U, step.digit)
                + " duration=" + std::to_string(step.onMs) + "ms gap="
                + std::to_string(step.offMs) + "ms enviando",
            {},
            correlationId));
        const std::uint64_t startedAt = util::monotonicMilliseconds();
        const auto sent = sendRfc4733(*call, step.digit, step.onMs);
        if (!sent) {
            static_cast<void>(logger_.log(
                logging::LogLevel::Error,
                "dtmf",
                "method=rfc4733 idx=" + std::to_string(digitIndex) + "/"
                    + std::to_string(digitCount) + " digit="
                    + std::string(1U, step.digit) + " status=ERROR",
                sent.error().detail,
                correlationId));
            return util::Result<DtmfResult>::failure(sent.error());
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(step.onMs + step.offMs));
        const std::uint64_t elapsed = util::monotonicMilliseconds() - startedAt;
        result.perDigit.push_back(DtmfDigitResult{step.digit, true, "OK"});
        static_cast<void>(logger_.log(
            logging::LogLevel::Info,
            "dtmf",
            "method=rfc4733 idx=" + std::to_string(digitIndex) + "/"
                + std::to_string(digitCount) + " digit=" + std::string(1U, step.digit)
                + " status=OK elapsed=" + std::to_string(elapsed) + "ms",
            {},
            correlationId));
    }

    result.ok = true;
    result.summary = std::to_string(digitCount) + "/" + std::to_string(digitCount)
        + " dígito(s) enviado(s) por RFC 4733.";
    static_cast<void>(events_.push(app::UiEvent{
        app::UiEventSeverity::Info,
        "dtmf",
        "id=" + correlationId + " concluído: " + result.summary}));
    return util::Result<DtmfResult>::success(std::move(result));
}

} // namespace polphone::dtmf
