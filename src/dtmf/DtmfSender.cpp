/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "dtmf/DtmfSender.h"

#include "audio/ToneGenerator.h"
#include "sip/CallRegistry.h"
#include "sip/PjErrors.h"
#include "sip/SipCall.h"
#include "util/Time.h"

#include <pjmedia/errno.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace polphone::dtmf {

util::Result<void> evaluateSipInfoResponse(
    int statusCode,
    std::string_view statusText,
    bool timedOut)
{
    const std::string detail = std::to_string(statusCode) + " "
        + std::string(statusText);
    if (!timedOut && statusCode >= 200 && statusCode < 300) {
        return util::Result<void>::success();
    }
    if (timedOut || statusCode == PJSIP_SC_REQUEST_TIMEOUT) {
        return util::Result<void>::failure(
            util::ErrorCode::Pjsip,
            "O SIP INFO expirou sem resposta; verifique a conectividade e o trace SIP.",
            detail);
    }
    switch (statusCode) {
    case PJSIP_SC_UNSUPPORTED_MEDIA_TYPE:
        return util::Result<void>::failure(
            util::ErrorCode::Pjsip,
            "O PABX rejeitou application/dtmf-relay (415); selecione explicitamente outro método ou ajuste o peer.",
            detail);
    case PJSIP_SC_CALL_TSX_DOES_NOT_EXIST:
        return util::Result<void>::failure(
            util::ErrorCode::Pjsip,
            "O diálogo SIP não existe mais (481); confirme se a chamada caiu.",
            detail);
    case PJSIP_SC_NOT_IMPLEMENTED:
        return util::Result<void>::failure(
            util::ErrorCode::Pjsip,
            "O peer não implementa DTMF por SIP INFO (501); selecione explicitamente outro método.",
            detail);
    default:
        return util::Result<void>::failure(
            util::ErrorCode::Pjsip,
            "O SIP INFO foi rejeitado; consulte o código e o trace SIP.",
            detail);
    }
}

DtmfSender::DtmfSender(
    sip::CallRegistry& calls,
    app::AppState& state,
    app::EventQueue& events,
    logging::Logger& logger) noexcept
    : calls_(calls), state_(state), events_(events), logger_(logger)
{
}

DtmfSender::~DtmfSender()
{
    cancelWorker_ = true;
    if (worker_.joinable()) worker_.join();
    toneGenerator_.reset();
}

util::Result<void> DtmfSender::configure(
    const config::DtmfConfig& config,
    const config::AudioConfig& audio)
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
    if (audio.clockRate <= 0 || audio.channelCount < 1 || audio.channelCount > 2
        || audio.ptimeMs <= 0 || audio.ptimeMs > 1000) {
        return util::Result<void>::failure(
            util::ErrorCode::Validation,
            "O formato de áudio é inválido para o gerador DTMF in-band.");
    }

    std::lock_guard<std::mutex> lock(settingsMutex_);
    settings_ = DtmfSettings{
        *method,
        static_cast<unsigned>(config.durationMs),
        static_cast<unsigned>(config.gapMs),
        config.volumeDbm0,
        config.localFeedback,
        config.logDigits};
    audioClockRate_ = static_cast<unsigned>(audio.clockRate);
    audioChannelCount_ = static_cast<unsigned>(audio.channelCount);
    audioPtimeMs_ = static_cast<unsigned>(audio.ptimeMs);
    return util::Result<void>::success();
}

DtmfSettings DtmfSender::settings() const
{
    std::lock_guard<std::mutex> lock(settingsMutex_);
    return settings_;
}

util::Result<void> DtmfSender::setDefaultMethod(DtmfMethod method)
{
    if (!executionPathFor(method).has_value()) {
        return util::Result<void>::failure(
            util::ErrorCode::InvalidArgument,
            "O método DTMF informado não é suportado.");
    }
    std::lock_guard<std::mutex> lock(settingsMutex_);
    settings_.defaultMethod = method;
    return util::Result<void>::success();
}

util::Result<void> DtmfSender::setDurationMs(int durationMs)
{
    if (durationMs < 40 || durationMs > 2000) {
        return util::Result<void>::failure(
            util::ErrorCode::InvalidArgument,
            "A duração DTMF deve estar entre 40 e 2000 ms.");
    }
    std::lock_guard<std::mutex> lock(settingsMutex_);
    settings_.durationMs = static_cast<unsigned>(durationMs);
    return util::Result<void>::success();
}

util::Result<void> DtmfSender::setGapMs(int gapMs)
{
    if (gapMs < 20 || gapMs > 2000) {
        return util::Result<void>::failure(
            util::ErrorCode::InvalidArgument,
            "O intervalo DTMF deve estar entre 20 e 2000 ms.");
    }
    std::lock_guard<std::mutex> lock(settingsMutex_);
    settings_.gapMs = static_cast<unsigned>(gapMs);
    return util::Result<void>::success();
}

util::Result<void> DtmfSender::setVolumeDbm0(int volumeDbm0)
{
    if (volumeDbm0 < -30 || volumeDbm0 > 0) {
        return util::Result<void>::failure(
            util::ErrorCode::InvalidArgument,
            "O volume DTMF deve estar entre -30 e 0 dBm0.");
    }
    std::lock_guard<std::mutex> lock(settingsMutex_);
    settings_.volumeDbm0 = volumeDbm0;
    return util::Result<void>::success();
}

bool DtmfSender::inFlight() const noexcept
{
    return requestGate_.inFlight();
}

DtmfSender::InFlightGuard::InFlightGuard(DtmfSender& sender) noexcept
    : sender_(sender)
{
}

DtmfSender::InFlightGuard::~InFlightGuard()
{
    if (active_) sender_.requestGate_.finish();
}

void DtmfSender::InFlightGuard::dismiss() noexcept
{
    active_ = false;
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

util::Result<void> DtmfSender::sendSipInfo(
    sip::SipCall& call,
    char digit,
    unsigned durationMs,
    std::string_view correlationId)
{
    const auto response = call.sendDtmfInfo(
        digit,
        durationMs,
        std::string(correlationId));
    if (!response) return util::Result<void>::failure(response.error());
    return evaluateSipInfoResponse(
        response.value().statusCode,
        response.value().statusText,
        response.value().timedOut);
}

std::string DtmfSender::nextCorrelationId()
{
    const unsigned long long value = sequence_.fetch_add(1U) + 1U;
    std::ostringstream output;
    output << "dtmf-" << std::setw(4) << std::setfill('0') << value;
    return output.str();
}

void DtmfSender::reapWorker() noexcept
{
    std::thread completed;
    try {
        if (inFlight()) return;
        std::lock_guard<std::mutex> lock(workerMutex_);
        if (worker_.joinable()) completed = std::move(worker_);
    } catch (...) {
        return;
    }
    if (completed.joinable()) completed.join();
}

bool DtmfSender::waitCancelable(unsigned milliseconds) const noexcept
{
    unsigned elapsed = 0U;
    while (elapsed < milliseconds) {
        if (cancelWorker_.load()) return false;
        const unsigned slice = (std::min)(10U, milliseconds - elapsed);
        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
        elapsed += slice;
    }
    return !cancelWorker_.load();
}

util::Result<DtmfResult> DtmfSender::startInband(
    sip::SipCall& call,
    std::vector<DtmfPlanStep> plan,
    const DtmfRequest& request,
    std::string correlationId)
{
    call.retainExternalUse();
    cancelWorker_ = false;
    try {
        std::lock_guard<std::mutex> lock(workerMutex_);
        if (worker_.joinable()) {
            call.releaseExternalUse();
            return util::Result<DtmfResult>::failure(
                util::ErrorCode::Runtime,
                "O worker DTMF anterior ainda não foi recolhido; tente novamente.");
        }
        worker_ = std::thread(
            &DtmfSender::runInband,
            this,
            &call,
            std::move(plan),
            request,
            correlationId);
    } catch (const std::exception& error) {
        call.releaseExternalUse();
        return util::Result<DtmfResult>::failure(
            util::ErrorCode::Runtime,
            "Não foi possível iniciar o worker DTMF in-band.",
            error.what());
    } catch (...) {
        call.releaseExternalUse();
        return util::Result<DtmfResult>::failure(
            util::ErrorCode::Runtime,
            "Falha desconhecida ao iniciar o worker DTMF in-band.");
    }

    DtmfResult result;
    result.ok = true;
    result.correlationId = std::move(correlationId);
    result.summary = "Envio DTMF in-band iniciado em segundo plano; id="
        + result.correlationId + ".";
    return util::Result<DtmfResult>::success(std::move(result));
}

util::Result<void> DtmfSender::performInband(
    sip::SipCall& call,
    const std::vector<DtmfPlanStep>& plan,
    const DtmfRequest& request,
    std::string_view correlationId)
{
    if (toneGenerator_ == nullptr) {
        try {
            toneGenerator_ = std::make_unique<audio::ToneGenerator>();
        } catch (const std::exception& error) {
            return util::Result<void>::failure(
                util::ErrorCode::Runtime,
                "Não foi possível reservar o gerador DTMF in-band.",
                error.what());
        }
    }
    const auto created = toneGenerator_->create(
        audioClockRate_, audioChannelCount_, audioPtimeMs_);
    if (!created) return created;

    const auto callMedia = call.audioMedia();
    if (!callMedia.has_value()) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "A porta de áudio da chamada não está disponível para DTMF in-band.");
    }
    const DtmfSettings current = settings();
    const auto connected = toneGenerator_->connect(*callMedia, current.localFeedback);
    if (!connected) return connected;

    static_cast<void>(logger_.log(
        logging::LogLevel::Info,
        "dtmf",
        "Gerador in-band conectado diretamente à chamada: slot="
            + std::to_string(toneGenerator_->bridgeSlot())
            + " clock=" + std::to_string(audioClockRate_) + "Hz ptime="
            + std::to_string(audioPtimeMs_) + "ms feedbackLocal="
            + (current.localFeedback ? "sim" : "não") + ".",
        {},
        correlationId));

    try {
        const pj::CallInfo information = call.getInfo();
        for (const auto& media : information.media) {
            if (media.type != PJMEDIA_TYPE_AUDIO
                || media.status != PJSUA_CALL_MEDIA_ACTIVE) {
                continue;
            }
            const pj::StreamInfo stream = call.getStreamInfo(media.index);
            const std::string codec = stream.codecName;
            const bool compatible = codec.find("PCMU") == 0U
                || codec.find("PCMA") == 0U || codec.find("G722") == 0U
                || codec.find("G.722") == 0U;
            if (!compatible) {
                static_cast<void>(logger_.log(
                    logging::LogLevel::Warning,
                    "dtmf",
                    "codec negociado=" + codec + "/"
                        + std::to_string(stream.codecClockRate)
                        + "; tons in-band podem não ser reconhecidos. Prosseguindo para registrar o experimento.",
                    {},
                    correlationId));
            }
            break;
        }
    } catch (const pj::Error& error) {
        static_cast<void>(logger_.log(
            logging::LogLevel::Warning,
            "dtmf",
            "Não foi possível confirmar o codec antes do tom in-band; prosseguindo.",
            sip::describe(error),
            correlationId));
    }

    std::size_t totalDigits = 0U;
    for (const auto& step : plan) {
        if (step.kind == DtmfPlanStep::Kind::Digit) ++totalDigits;
    }
    std::size_t globalIndex = 0U;
    std::size_t planIndex = 0U;
    while (planIndex < plan.size()) {
        if (cancelWorker_.load()) {
            return util::Result<void>::failure(
                util::ErrorCode::Runtime,
                "O envio DTMF in-band foi cancelado pelo encerramento.");
        }
        const auto active = activeCall();
        if (!active || active.value() != &call) {
            return util::Result<void>::failure(
                util::ErrorCode::Runtime,
                "A chamada terminou ou mudou durante o DTMF in-band.");
        }
        if (plan[planIndex].kind == DtmfPlanStep::Kind::Pause) {
            if (!waitCancelable(plan[planIndex].pauseMs)) {
                return util::Result<void>::failure(
                    util::ErrorCode::Runtime,
                    "O envio DTMF in-band foi cancelado durante uma pausa.");
            }
            ++planIndex;
            continue;
        }

        std::vector<audio::ToneDigit> batch;
        std::vector<std::uint64_t> completionThresholds;
        std::uint64_t expectedMs = 0U;
        while (planIndex < plan.size()
               && plan[planIndex].kind == DtmfPlanStep::Kind::Digit
               && batch.size() < audio::ToneGenerator::MaxQueuedDigits) {
            const auto& step = plan[planIndex++];
            ++globalIndex;
            batch.push_back(audio::ToneDigit{
                step.digit, step.onMs, step.offMs, request.volumeDbm0});
            expectedMs += static_cast<std::uint64_t>(step.onMs) + step.offMs;
            completionThresholds.push_back(expectedMs);
            static_cast<void>(logger_.log(
                logging::LogLevel::Info,
                "dtmf",
                "method=inband idx=" + std::to_string(globalIndex) + "/"
                    + std::to_string(totalDigits) + " digit="
                    + std::string(1U, step.digit) + " duration="
                    + std::to_string(step.onMs) + "ms gap="
                    + std::to_string(step.offMs) + "ms volume="
                    + std::to_string(request.volumeDbm0) + "dBm0 enfileirando",
                {},
                correlationId));
        }

        const auto played = toneGenerator_->playDigits(batch);
        if (!played) return played;
        const std::uint64_t startedAt = util::monotonicMilliseconds();
        std::size_t completedInBatch = 0U;
        while (toneGenerator_->isBusy()) {
            if (cancelWorker_.load()) {
                return util::Result<void>::failure(
                    util::ErrorCode::Runtime,
                    "O envio DTMF in-band foi cancelado pelo encerramento.");
            }
            const auto stillActive = activeCall();
            if (!stillActive || stillActive.value() != &call) {
                return util::Result<void>::failure(
                    util::ErrorCode::Runtime,
                    "A chamada terminou durante a reprodução DTMF in-band.");
            }
            const std::uint64_t elapsed =
                util::monotonicMilliseconds() - startedAt;
            while (completedInBatch < batch.size()
                   && elapsed >= completionThresholds[completedInBatch]) {
                const std::size_t absoluteIndex =
                    globalIndex - batch.size() + completedInBatch + 1U;
                static_cast<void>(logger_.log(
                    logging::LogLevel::Info,
                    "dtmf",
                    "method=inband idx=" + std::to_string(absoluteIndex) + "/"
                        + std::to_string(totalDigits) + " digit="
                        + std::string(1U, batch[completedInBatch].digit)
                        + " status=OK elapsed=" + std::to_string(elapsed) + "ms",
                    {},
                    correlationId));
                ++completedInBatch;
            }
            if (elapsed > expectedMs + 500U) {
                return util::Result<void>::failure(
                    util::ErrorCode::Runtime,
                    "O gerador DTMF in-band excedeu o timeout de segurança.",
                    "esperado=" + std::to_string(expectedMs) + "ms");
            }
            static_cast<void>(waitCancelable(10U));
        }
        const std::uint64_t elapsed = util::monotonicMilliseconds() - startedAt;
        while (completedInBatch < batch.size()) {
            const std::size_t absoluteIndex =
                globalIndex - batch.size() + completedInBatch + 1U;
            static_cast<void>(logger_.log(
                logging::LogLevel::Info,
                "dtmf",
                "method=inband idx=" + std::to_string(absoluteIndex) + "/"
                    + std::to_string(totalDigits) + " digit="
                    + std::string(1U, batch[completedInBatch].digit)
                    + " status=OK elapsed=" + std::to_string(elapsed) + "ms",
                {},
                correlationId));
            ++completedInBatch;
        }
    }
    return util::Result<void>::success();
}

void DtmfSender::publishInbandFailure(
    std::string_view correlationId,
    const util::Error& error) noexcept
{
    try {
        static_cast<void>(logger_.log(
            logging::LogLevel::Error,
            "dtmf",
            "method=inband status=ERROR " + error.message,
            error.detail,
            correlationId));
        static_cast<void>(events_.push(app::UiEvent{
            app::UiEventSeverity::Error,
            "dtmf",
            "id=" + std::string(correlationId) + " falhou: " + error.message}));
    } catch (...) {
    }
}

void DtmfSender::runInband(
    sip::SipCall* call,
    std::vector<DtmfPlanStep> plan,
    DtmfRequest request,
    std::string correlationId) noexcept
{
    bool success = false;
    bool reported = false;
    try {
        pj::Endpoint& endpoint = pj::Endpoint::instance();
        if (!endpoint.libIsThreadRegistered()) {
            endpoint.libRegisterThread("polphone-dtmf");
        }
        const auto performed = performInband(
            *call, plan, request, correlationId);
        success = static_cast<bool>(performed);
        if (!performed) {
            publishInbandFailure(correlationId, performed.error());
            reported = true;
        }
    } catch (const pj::Error& error) {
        try {
            const util::Error translated = sip::makePjError(
                error, "executar DTMF in-band");
            publishInbandFailure(correlationId, translated);
            reported = true;
        } catch (...) {
        }
    } catch (const std::exception& error) {
        try {
            const util::Error translated{
                util::ErrorCode::Runtime,
                "Falha inesperada durante o DTMF in-band.",
                error.what()};
            publishInbandFailure(correlationId, translated);
            reported = true;
        } catch (...) {
        }
    } catch (...) {
    }

    if (toneGenerator_ != nullptr) {
        try {
            static_cast<void>(toneGenerator_->stop());
        } catch (...) {
        }
        toneGenerator_->disconnect();
    }
    call->releaseExternalUse();
    if (success) {
        try {
            static_cast<void>(events_.push(app::UiEvent{
                app::UiEventSeverity::Info,
                "dtmf",
                "id=" + correlationId + " concluído por in-band."}));
        } catch (...) {
        }
    } else if (!reported) {
        try {
            const util::Error unknown{
                util::ErrorCode::Runtime,
                "Falha desconhecida durante o DTMF in-band.",
                {}};
            publishInbandFailure(correlationId, unknown);
        } catch (...) {
        }
    }
    requestGate_.finish();
}

util::Result<DtmfResult> DtmfSender::send(const DtmfRequest& request)
{
    reapWorker();
    auto plan = DtmfPlan::build(request.digits, request.durationMs, request.gapMs);
    if (!plan) return util::Result<DtmfResult>::failure(plan.error());
    if (request.volumeDbm0 < -30 || request.volumeDbm0 > 0) {
        return util::Result<DtmfResult>::failure(
            util::ErrorCode::InvalidArgument,
            "O volume DTMF deve estar entre -30 e 0 dBm0.");
    }
    const auto executionPath = executionPathFor(request.method);
    if (!executionPath.has_value()) {
        return util::Result<DtmfResult>::failure(
            util::ErrorCode::InvalidArgument,
            "O método DTMF informado não possui um caminho de execução.");
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
    const auto started = requestGate_.begin(correlationId);
    if (!started) return util::Result<DtmfResult>::failure(started.error());
    InFlightGuard flight(*this);

    const auto active = activeCall();
    if (!active) return util::Result<DtmfResult>::failure(active.error());
    sip::SipCall* call = active.value();

    if (*executionPath == DtmfExecutionPath::Inband) {
        auto inband = startInband(
            *call, std::move(plan).value(), request, std::move(correlationId));
        if (inband) flight.dismiss();
        return inband;
    }

    if (*executionPath == DtmfExecutionPath::Rfc4733) {
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
    const std::string method = std::string(methodName(request.method));
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
            "method=" + method + " idx=" + std::to_string(digitIndex) + "/"
                + std::to_string(digitCount) + " digit=" + std::string(1U, step.digit)
                + " duration=" + std::to_string(step.onMs) + "ms gap="
                + std::to_string(step.offMs) + "ms enviando",
            {},
            correlationId));
        const std::uint64_t startedAt = util::monotonicMilliseconds();
        const auto sent = *executionPath == DtmfExecutionPath::Rfc4733
            ? sendRfc4733(*call, step.digit, step.onMs)
            : sendSipInfo(*call, step.digit, step.onMs, correlationId);
        if (!sent) {
            static_cast<void>(logger_.log(
                logging::LogLevel::Error,
                "dtmf",
                "method=" + method + " idx=" + std::to_string(digitIndex) + "/"
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
            "method=" + method + " idx=" + std::to_string(digitIndex) + "/"
                + std::to_string(digitCount) + " digit=" + std::string(1U, step.digit)
                + " status=OK elapsed=" + std::to_string(elapsed) + "ms",
            {},
            correlationId));
    }

    result.ok = true;
    result.summary = std::to_string(digitCount) + "/" + std::to_string(digitCount)
        + " dígito(s) enviado(s) por "
        + (request.method == DtmfMethod::Rfc4733 ? "RFC 4733." : "SIP INFO.");
    static_cast<void>(events_.push(app::UiEvent{
        app::UiEventSeverity::Info,
        "dtmf",
        "id=" + correlationId + " concluído: " + result.summary}));
    return util::Result<DtmfResult>::success(std::move(result));
}

} // namespace polphone::dtmf
