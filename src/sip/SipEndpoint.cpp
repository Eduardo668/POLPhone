/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "sip/SipEndpoint.h"

#include "logging/PjLogWriter.h"
#include "sip/PjErrors.h"
#include "util/Strings.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace polphone::sip {
namespace {

util::Result<void> runtimeFailure(std::string message, std::string detail = {})
{
    return util::Result<void>::failure(
        util::ErrorCode::Runtime, std::move(message), std::move(detail));
}

} // namespace

SipEndpoint::~SipEndpoint()
{
    static_cast<void>(destroy());
}

util::Result<void> SipEndpoint::invalidState(std::string_view operation) const
{
    return util::Result<void>::failure(
        util::ErrorCode::Runtime,
        "Ciclo de vida inválido do endpoint; encerre e reinicie a aplicação.",
        std::string(operation));
}

util::Result<void> SipEndpoint::create()
{
    if (state_ != State::Empty || endpoint_ != nullptr) return invalidState("create");
    try {
        endpoint_ = std::make_unique<pj::Endpoint>();
        const auto created = POLPHONE_PJ_TRY(endpoint_->libCreate());
        if (!created) {
            endpoint_.reset();
            return created;
        }
        state_ = State::Created;
        return util::Result<void>::success();
    } catch (const pj::Error& error) {
        endpoint_.reset();
        return util::Result<void>::failure(makePjError(error, "pj::Endpoint::Endpoint"));
    } catch (const std::exception&) {
        endpoint_.reset();
        return runtimeFailure(
            "Não foi possível criar o endpoint; verifique memória disponível e reinicie.");
    } catch (...) {
        endpoint_.reset();
        return runtimeFailure("Falha desconhecida ao criar o endpoint; reinicie a aplicação.");
    }
}

util::Result<void> SipEndpoint::init(const config::AppConfig& appConfig,
                                     logging::Logger& logger,
                                     int technicalLogLevel)
{
    if (state_ != State::Created || endpoint_ == nullptr) return invalidState("init");
    try {
        pj::EpConfig endpointConfig;
        endpointConfig.uaConfig.threadCnt = 1U;
        endpointConfig.uaConfig.maxCalls = 4U;
        endpointConfig.uaConfig.userAgent = "POLPhone/0.1 (PJSIP 2.17)";

        const int boundedLogLevel = std::clamp(technicalLogLevel, 0, 6);
        endpointConfig.logConfig.level = boundedLogLevel;
        // Na tag 2.17, consoleLevel também limita o callback customizado.
        endpointConfig.logConfig.consoleLevel = boundedLogLevel;
        endpointConfig.logConfig.msgLogging = appConfig.logging.sipMessageTrace ? 1 : 0;
        endpointConfig.logConfig.writer = new logging::PjLogWriter(logger);

        endpointConfig.medConfig.clockRate =
            static_cast<unsigned>(appConfig.audio.clockRate);
        endpointConfig.medConfig.channelCount =
            static_cast<unsigned>(appConfig.audio.channelCount);
        endpointConfig.medConfig.audioFramePtime =
            static_cast<unsigned>(appConfig.audio.ptimeMs);
        endpointConfig.medConfig.ecTailLen =
            static_cast<unsigned>(appConfig.audio.ecTailMs);
        endpointConfig.medConfig.quality =
            static_cast<unsigned>(appConfig.audio.quality);
        // O in-band depende de VAD desativado; a validação permite apenas a
        // escolha explícita do arquivo, mas o endpoint aplica a invariante MVP.
        endpointConfig.medConfig.noVad = true;

        const auto initialized = POLPHONE_PJ_TRY(endpoint_->libInit(endpointConfig));
        if (!initialized) return initialized;
        logger_ = &logger;
        state_ = State::Initialized;
        return util::Result<void>::success();
    } catch (const std::bad_alloc&) {
        return runtimeFailure(
            "Memória insuficiente ao inicializar o endpoint; feche outros aplicativos e tente novamente.");
    } catch (const std::exception& error) {
        return runtimeFailure(
            "Falha inesperada ao montar a configuração do endpoint; revise a configuração.",
            error.what());
    } catch (...) {
        return runtimeFailure(
            "Falha desconhecida ao inicializar o endpoint; reinicie a aplicação.");
    }
}

util::Result<void> SipEndpoint::createUdpTransport(const config::NetworkConfig& networkConfig)
{
    if (state_ != State::Initialized || endpoint_ == nullptr || logger_ == nullptr) {
        return invalidState("createUdpTransport");
    }
    pj::TransportConfig transportConfig;
    transportConfig.port = static_cast<unsigned>(networkConfig.localPort);
    transportConfig.boundAddress = networkConfig.boundAddress;
    const auto created = POLPHONE_PJ_TRY(
        endpoint_->transportCreate(PJSIP_TRANSPORT_UDP, transportConfig));
    if (!created) return util::Result<void>::failure(created.error());
    transportId_ = created.value();

    const auto information = POLPHONE_PJ_TRY(endpoint_->transportGetInfo(transportId_));
    if (!information) return util::Result<void>::failure(information.error());
    static_cast<void>(logger_->info(
        "transport",
        "Transporte SIP UDP ativo em " + information.value().localAddress + "."));
    state_ = State::TransportReady;
    return util::Result<void>::success();
}

util::Result<void> SipEndpoint::start()
{
    if (state_ != State::TransportReady || endpoint_ == nullptr) return invalidState("start");
    const auto started = POLPHONE_PJ_TRY(endpoint_->libStart());
    if (!started) return started;
    state_ = State::Started;
    return util::Result<void>::success();
}

util::Result<void> SipEndpoint::applyCodecPriorities(const config::CodecsConfig& codecConfig)
{
    if (state_ != State::Started || endpoint_ == nullptr || logger_ == nullptr) {
        return invalidState("applyCodecPriorities");
    }

    const auto availableCodecs = POLPHONE_PJ_TRY(endpoint_->codecEnum2());
    if (!availableCodecs) return util::Result<void>::failure(availableCodecs.error());

    for (const auto& configured : codecConfig.priority) {
        const auto available = std::find_if(
            availableCodecs.value().cbegin(),
            availableCodecs.value().cend(),
            [&configured](const pj::CodecInfo& codec) {
                return util::iequals(codec.codecId, configured.first);
            });
        if (available == availableCodecs.value().cend()) {
            if (configured.second == 0) {
                static_cast<void>(logger_->debug(
                    "codec",
                    "Codec desabilitado não está disponível neste build: " + configured.first));
                continue;
            }
            return util::Result<void>::failure(
                util::ErrorCode::Pjsip,
                "Um codec habilitado na configuração não está disponível neste build.",
                "codec=" + configured.first);
        }

        const auto applied = POLPHONE_PJ_TRY(endpoint_->codecSetPriority(
            available->codecId, static_cast<pj_uint8_t>(configured.second)));
        if (!applied) return applied;
    }

    const auto codecs = POLPHONE_PJ_TRY(endpoint_->codecEnum2());
    if (!codecs) return util::Result<void>::failure(codecs.error());
    for (const auto& codec : codecs.value()) {
        static_cast<void>(logger_->info(
            "codec",
            "Codec efetivo: id=" + codec.codecId
                + " priority=" + std::to_string(static_cast<unsigned>(codec.priority))
                + " desc=" + codec.desc));
    }
    return util::Result<void>::success();
}

util::Result<void> SipEndpoint::registerThisThread(std::string_view name)
{
    if (state_ == State::Empty || endpoint_ == nullptr) return invalidState("registerThisThread");
    const auto registered = POLPHONE_PJ_TRY(endpoint_->libIsThreadRegistered());
    if (!registered) return util::Result<void>::failure(registered.error());
    if (registered.value()) return util::Result<void>::success();
    return POLPHONE_PJ_TRY(endpoint_->libRegisterThread(std::string(name)));
}

util::Result<void> SipEndpoint::destroy() noexcept
{
    if (endpoint_ == nullptr) {
        state_ = State::Empty;
        logger_ = nullptr;
        transportId_ = pj::INVALID_ID;
        return util::Result<void>::success();
    }

    util::Result<void> destroyed = util::Result<void>::success();
    try {
        destroyed = POLPHONE_PJ_TRY(endpoint_->libDestroy());
    } catch (const std::exception& error) {
        destroyed = runtimeFailure(
            "Falha inesperada ao destruir o endpoint; o processo será encerrado.",
            error.what());
    } catch (...) {
        destroyed = runtimeFailure(
            "Falha desconhecida ao destruir o endpoint; o processo será encerrado.");
    }
    endpoint_.reset();
    state_ = State::Empty;
    logger_ = nullptr;
    transportId_ = pj::INVALID_ID;
    return destroyed;
}

bool SipEndpoint::isCreated() const noexcept
{
    return state_ != State::Empty && endpoint_ != nullptr;
}

bool SipEndpoint::isStarted() const noexcept
{
    return state_ == State::Started && endpoint_ != nullptr;
}

pj::Endpoint* SipEndpoint::native() noexcept
{
    return endpoint_.get();
}

} // namespace polphone::sip
