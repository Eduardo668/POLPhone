/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "app/Application.h"

#include "app/ConsoleUi.h"
#include "audio/ToneGenerator.h"
#include "config/ConfigLoader.h"
#include "config/ConfigValidator.h"
#include "logging/Logger.h"
#include "sip/SipCall.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <utility>

namespace polphone::app {

Application::Application(ApplicationOptions options)
    : options_(std::move(options))
{
}

Application::~Application()
{
    shutdown();
}

util::Result<void> Application::failInitialization(util::Error error)
{
    static_cast<void>(logger_.log(
        logging::LogLevel::Fatal, "app", error.message, error.detail));
    shutdown();
    return util::Result<void>::failure(std::move(error));
}

util::Result<void> Application::initialize()
{
    if (initialized_) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "A aplicação já foi inicializada; encerre a instância antes de reiniciar.");
    }
    shutdownStarted_ = false;

    config::ConfigLoader::Warnings warnings;
    if (options_.useBuiltInConfig) {
        config_.emplace();
    } else {
        auto loaded = config::ConfigLoader::load(options_.configPath, &warnings);
        if (!loaded) return util::Result<void>::failure(loaded.error());
        const auto validated = config::ConfigValidator::validate(loaded.value());
        if (!validated) return util::Result<void>::failure(validated.error());
        config_.emplace(std::move(loaded).value());
    }

    const int consoleLevelNumber =
        options_.consoleLogLevel.value_or(config_->logging.consoleLevel);
    try {
        consoleSink_ = std::make_shared<logging::ConsoleLogSink>(std::cout);
    } catch (...) {
        return failInitialization(util::Error{
            util::ErrorCode::Runtime,
            "Falha ao reservar o console de log; libere memória e tente novamente.",
            {}});
    }
    if (!logger_.addSink(
            consoleSink_, logging::logLevelFromNumber(consoleLevelNumber))) {
        return failInitialization(util::Error{
            util::ErrorCode::Runtime,
            "Falha ao inicializar o console de log; verifique a saída padrão.",
            "Logger::addSink"});
    }
    consoleLogLevel_ = consoleLevelNumber;
    dtmfMethod_ = config_->dtmf.defaultMethod;
    logger_.setLogDtmfDigits(config_->dtmf.logDigits);

    const std::uintmax_t maxFileBytes =
        static_cast<std::uintmax_t>(config_->logging.maxFileMB) * 1024U * 1024U;
    const auto logFile = logger_.enableFile(
        config_->logging.directory,
        logging::logLevelFromNumber(config_->logging.fileLevel),
        maxFileBytes);
    if (!logFile) {
        static_cast<void>(logger_.warning(
            "logging", "Arquivo de log indisponível; continuando somente no console."));
    }
    if (config_->dtmf.logDigits) {
        static_cast<void>(logger_.warning(
            "dtmf",
            "dtmf.logDigits=true — dígitos serão gravados em claro no log. Não use com dados sensíveis."));
    }
    for (const auto& warning : warnings) {
        static_cast<void>(logger_.warning("config", warning));
    }
    static_cast<void>(logger_.info("config", config_->redactedDump()));

    try {
        endpoint_ = std::make_unique<sip::SipEndpoint>();
    } catch (const std::exception& error) {
        return failInitialization(util::Error{
            util::ErrorCode::Runtime,
            "Não foi possível reservar o endpoint; libere memória e tente novamente.",
            error.what()});
    } catch (...) {
        return failInitialization(util::Error{
            util::ErrorCode::Runtime,
            "Falha desconhecida ao reservar o endpoint; reinicie a aplicação.",
            {}});
    }

    if (const auto result = endpoint_->create(); !result) {
        return failInitialization(result.error());
    }
    const int technicalLogLevel =
        (std::max)(consoleLevelNumber, config_->logging.fileLevel);
    if (const auto result = endpoint_->init(*config_, logger_, technicalLogLevel); !result) {
        return failInitialization(result.error());
    }
    if (const auto result = endpoint_->createUdpTransport(config_->network); !result) {
        return failInitialization(result.error());
    }
    if (const auto result = endpoint_->start(); !result) {
        return failInitialization(result.error());
    }
    if (const auto result = endpoint_->applyCodecPriorities(config_->codecs); !result) {
        return failInitialization(result.error());
    }
    try {
        audioDevices_ = std::make_unique<audio::AudioDeviceService>(
            *endpoint_->native(), logger_);
    } catch (const std::exception& error) {
        return failInitialization(util::Error{
            util::ErrorCode::Runtime,
            "Não foi possível reservar o serviço de áudio; libere memória e tente novamente.",
            error.what()});
    } catch (...) {
        return failInitialization(util::Error{
            util::ErrorCode::Runtime,
            "Falha desconhecida ao reservar o serviço de áudio; reinicie a aplicação.",
            {}});
    }
    if (const auto result = audioDevices_->apply(config_->audio); !result) {
        return failInitialization(result.error());
    }
    if (!options_.selftest && !options_.listDevices) {
        try {
            account_ = std::make_unique<sip::SipAccount>(
                state_, events_, logger_, calls_);
        } catch (const std::exception& error) {
            return failInitialization(util::Error{
                util::ErrorCode::Runtime,
                "Não foi possível reservar a conta SIP; libere memória e tente novamente.",
                error.what()});
        } catch (...) {
            return failInitialization(util::Error{
                util::ErrorCode::Runtime,
                "Falha desconhecida ao reservar a conta SIP; reinicie a aplicação.",
                {}});
        }
        if (const auto result = account_->createFrom(config_->sip); !result) {
            return failInitialization(result.error());
        }
        try {
            dtmfSender_ = std::make_unique<dtmf::DtmfSender>(
                calls_, state_, events_, logger_);
        } catch (const std::exception& error) {
            return failInitialization(util::Error{
                util::ErrorCode::Runtime,
                "Não foi possível reservar o serviço DTMF.",
                error.what()});
        } catch (...) {
            return failInitialization(util::Error{
                util::ErrorCode::Runtime,
                "Falha desconhecida ao reservar o serviço DTMF.",
                {}});
        }
        if (const auto result = dtmfSender_->configure(
                config_->dtmf, config_->audio);
            !result) {
            return failInitialization(result.error());
        }
        try {
            console_ = std::make_unique<ConsoleUi>(
                *this, std::cin, std::cout, std::cerr);
        } catch (const std::exception& error) {
            return failInitialization(util::Error{
                util::ErrorCode::Runtime,
                "Não foi possível reservar o console interativo.",
                error.what()});
        } catch (...) {
            return failInitialization(util::Error{
                util::ErrorCode::Runtime,
                "Falha desconhecida ao reservar o console interativo.",
                {}});
        }
    }

    initialized_ = true;
    static_cast<void>(logger_.info("app", "Inicialização do endpoint concluída."));
    return util::Result<void>::success();
}

int Application::run()
{
    if (!initialized_) {
        static_cast<void>(logger_.error(
            "app", "Aplicação não inicializada; execute novamente e revise os erros anteriores."));
        return 3;
    }
    if (options_.listDevices) {
        const auto devices = audioDevices_->list();
        if (!devices) {
            static_cast<void>(logger_.log(
                logging::LogLevel::Error,
                "audio",
                devices.error().message,
                devices.error().detail));
            return 3;
        }
        std::cout << "Dispositivos de áudio (WMME):\n";
        for (const auto& device : devices.value()) {
            std::cout << "  #" << device.id
                      << " [in:" << device.inputCount << "]"
                      << " [out:" << device.outputCount << "] "
                      << device.name << " — " << device.driver
                      << " @ " << device.defaultSamplesPerSec << " Hz\n";
        }
        return 0;
    }
    if (options_.selftest) {
        for (unsigned iteration = 0U; iteration < 50U; ++iteration) {
            audio::ToneGenerator toneGenerator;
            const auto created = toneGenerator.create(
                static_cast<unsigned>(config_->audio.clockRate),
                static_cast<unsigned>(config_->audio.channelCount),
                static_cast<unsigned>(config_->audio.ptimeMs));
            if (!created) {
                static_cast<void>(logger_.log(
                    logging::LogLevel::Error,
                    "dtmf",
                    "Selftest do ciclo de vida do tonegen falhou.",
                    created.error().detail));
                return 3;
            }
        }
        static_cast<void>(logger_.info(
            "dtmf",
            "Selftest do tonegen concluído: 50 registros/remoções da conference bridge."));
        static_cast<void>(logger_.info(
            "app", "Selftest do endpoint, transporte e codecs concluído com sucesso."));
    }
    if (console_ != nullptr) return console_->run();
    static_cast<void>(calls_.reap());
    for (const auto& event : events_.drain()) {
        const auto level = event.severity == UiEventSeverity::Error
            ? logging::LogLevel::Error
            : event.severity == UiEventSeverity::Warning
                ? logging::LogLevel::Warning
                : logging::LogLevel::Info;
        static_cast<void>(logger_.log(level, event.category, event.text));
    }
    return 0;
}

util::Result<void> Application::makeCall(std::string_view destination)
{
    if (!initialized_ || account_ == nullptr || config_ == std::nullopt) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "A conta SIP não está pronta; inicialize a aplicação antes de ligar.");
    }
    const auto normalized = sip::normalizeDestination(destination, config_->sip.domain);
    if (!normalized) return util::Result<void>::failure(normalized.error());

    std::unique_ptr<sip::SipCall> call;
    try {
        call = std::make_unique<sip::SipCall>(
            *account_, calls_, state_, events_, logger_);
    } catch (const std::exception& error) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Não foi possível reservar a chamada; libere memória e tente novamente.",
            error.what());
    } catch (...) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Falha desconhecida ao reservar a chamada; reinicie a aplicação.");
    }

    sip::SipCall* adoptedCall = call.get();
    if (const auto adopted = calls_.adopt(call); !adopted) return adopted;
    const auto started = adoptedCall->start(normalized.value());
    if (!started) {
        calls_.retire(adoptedCall);
        static_cast<void>(calls_.waitUntilSafeToReap(std::chrono::seconds(1)));
        static_cast<void>(calls_.reap());
        return started;
    }
    return util::Result<void>::success();
}

util::Result<void> Application::answerCall()
{
    sip::SipCall* call = calls_.current();
    if (call == nullptr) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Não existe chamada ativa para atender.");
    }
    return call->answer(PJSIP_SC_OK);
}

util::Result<void> Application::hangupCall()
{
    sip::SipCall* call = calls_.current();
    if (call == nullptr) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Não existe chamada ativa para encerrar.");
    }
    return call->hangupCall();
}

util::Result<void> Application::setRegistrationEnabled(bool enabled)
{
    if (!initialized_ || account_ == nullptr) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "A conta SIP não está pronta; revise a inicialização.");
    }
    return account_->setRegistrationEnabled(enabled);
}

util::Result<std::vector<audio::AudioDeviceDescription>>
Application::listAudioDevices() const
{
    if (!initialized_ || audioDevices_ == nullptr) {
        return util::Result<std::vector<audio::AudioDeviceDescription>>::failure(
            util::ErrorCode::Runtime,
            "O serviço de áudio não está pronto; revise a inicialização.");
    }
    return audioDevices_->list();
}

util::Result<void> Application::selectAudioDevice(
    audio::AudioDeviceDirection direction,
    int id)
{
    if (!initialized_ || audioDevices_ == nullptr) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "O serviço de áudio não está pronto; revise a inicialização.");
    }
    const bool callActive = calls_.hasActiveCall();
    return direction == audio::AudioDeviceDirection::Capture
        ? audioDevices_->selectCapture(id, callActive)
        : audioDevices_->selectPlayback(id, callActive);
}

util::Result<std::vector<sip::EffectiveCodec>> Application::listCodecs()
{
    if (!initialized_ || endpoint_ == nullptr) {
        return util::Result<std::vector<sip::EffectiveCodec>>::failure(
            util::ErrorCode::Runtime,
            "O endpoint SIP não está pronto; revise a inicialização.");
    }
    return endpoint_->listCodecs();
}

util::Result<void> Application::setConsoleLogLevel(int level)
{
    if (level < 0 || level > 6) {
        return util::Result<void>::failure(
            util::ErrorCode::InvalidArgument,
            "O nível de log deve estar entre 0 e 6.");
    }
    if (!logger_.setSinkLevel(consoleSink_, logging::logLevelFromNumber(level))) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Não foi possível alterar o nível do console de log.");
    }
    consoleLogLevel_ = level;
    return util::Result<void>::success();
}

util::Result<void> Application::setDtmfDefaultMethod(dtmf::DtmfMethod method)
{
    if (!initialized_ || dtmfSender_ == nullptr) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "O serviço DTMF não está pronto; revise a inicialização.");
    }
    const auto changed = dtmfSender_->setDefaultMethod(method);
    if (!changed) return changed;
    dtmfMethod_ = std::string(dtmf::methodName(method));
    static_cast<void>(logger_.info(
        "dtmf", "Método padrão da sessão alterado para " + dtmfMethod_ + "."));
    return util::Result<void>::success();
}

util::Result<void> Application::setDtmfDurationMs(int durationMs)
{
    if (!initialized_ || dtmfSender_ == nullptr) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "O serviço DTMF não está pronto; revise a inicialização.");
    }
    const auto changed = dtmfSender_->setDurationMs(durationMs);
    if (!changed) return changed;
    static_cast<void>(logger_.info(
        "dtmf", "Duração padrão alterada para " + std::to_string(durationMs) + " ms."));
    return util::Result<void>::success();
}

util::Result<void> Application::setDtmfGapMs(int gapMs)
{
    if (!initialized_ || dtmfSender_ == nullptr) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "O serviço DTMF não está pronto; revise a inicialização.");
    }
    const auto changed = dtmfSender_->setGapMs(gapMs);
    if (!changed) return changed;
    static_cast<void>(logger_.info(
        "dtmf", "Intervalo padrão alterado para " + std::to_string(gapMs) + " ms."));
    return util::Result<void>::success();
}

util::Result<void> Application::setDtmfVolumeDbm0(int volumeDbm0)
{
    if (!initialized_ || dtmfSender_ == nullptr) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "O serviço DTMF não está pronto; revise a inicialização.");
    }
    const auto changed = dtmfSender_->setVolumeDbm0(volumeDbm0);
    if (!changed) return changed;
    static_cast<void>(logger_.info(
        "dtmf", "Volume in-band padrão alterado para "
            + std::to_string(volumeDbm0) + " dBm0."));
    return util::Result<void>::success();
}

util::Result<dtmf::DtmfResult> Application::sendDtmf(
    std::string_view digits,
    std::optional<dtmf::DtmfMethod> method,
    std::optional<int> durationMs,
    std::optional<int> gapMs)
{
    if (!initialized_ || dtmfSender_ == nullptr) {
        return util::Result<dtmf::DtmfResult>::failure(
            util::ErrorCode::Runtime,
            "O serviço DTMF não está pronto; revise a inicialização.");
    }
    const dtmf::DtmfSettings settings = dtmfSender_->settings();
    const int duration = durationMs.value_or(static_cast<int>(settings.durationMs));
    const int gap = gapMs.value_or(static_cast<int>(settings.gapMs));
    if (duration < 0 || gap < 0) {
        return util::Result<dtmf::DtmfResult>::failure(
            util::ErrorCode::InvalidArgument,
            "Duração e intervalo DTMF não podem ser negativos.");
    }
    return dtmfSender_->send(dtmf::DtmfRequest{
        std::string(digits),
        method.value_or(settings.defaultMethod),
        static_cast<unsigned>(duration),
        static_cast<unsigned>(gap),
        settings.volumeDbm0});
}

ApplicationStatus Application::status() const
{
    ApplicationStatus snapshot;
    snapshot.registration = state_.registration();
    snapshot.call = state_.call();
    if (audioDevices_ != nullptr) {
        snapshot.captureDevice = audioDevices_->selectedCapture();
        snapshot.playbackDevice = audioDevices_->selectedPlayback();
    }
    if (config_.has_value()) snapshot.dtmf = config_->dtmf;
    if (dtmfSender_ != nullptr) {
        const auto settings = dtmfSender_->settings();
        snapshot.dtmf.defaultMethod = std::string(dtmf::methodName(settings.defaultMethod));
        snapshot.dtmf.durationMs = static_cast<int>(settings.durationMs);
        snapshot.dtmf.gapMs = static_cast<int>(settings.gapMs);
        snapshot.dtmf.volumeDbm0 = settings.volumeDbm0;
        snapshot.dtmfMethod = std::string(dtmf::methodName(settings.defaultMethod));
    } else {
        snapshot.dtmfMethod = dtmfMethod_;
    }
    snapshot.consoleLogLevel = consoleLogLevel_;
    return snapshot;
}

std::vector<UiEvent> Application::drainEvents()
{
    return events_.drain();
}

std::size_t Application::reapCalls() noexcept
{
    return calls_.reap();
}

void Application::shutdown() noexcept
{
    if (shutdownStarted_) return;
    shutdownStarted_ = true;
    initialized_ = false;
    console_.reset();
    dtmfSender_.reset();

    if (sip::SipCall* call = calls_.current(); call != nullptr) {
        const auto hungUp = call->hangupCall();
        if (!hungUp) {
            static_cast<void>(logger_.warning(
                "call", hungUp.error().message + " " + hungUp.error().detail));
        }
        static_cast<void>(calls_.waitUntilIdle(std::chrono::seconds(3)));
    }
    if (endpoint_ != nullptr && endpoint_->isStarted()) {
        const auto allHungUp = endpoint_->hangupAllCalls();
        if (!allHungUp) {
            static_cast<void>(logger_.warning(
                "call", allHungUp.error().message + " " + allHungUp.error().detail));
        }
        static_cast<void>(calls_.waitUntilIdle(std::chrono::seconds(1)));
    }
    if (sip::SipCall* remaining = calls_.current(); remaining != nullptr) {
        calls_.retire(remaining);
    }

    if (account_ != nullptr) {
        const auto unregistered = account_->unregisterAndWait();
        if (!unregistered) {
            static_cast<void>(logger_.warning(
                "sip", unregistered.error().message + " " + unregistered.error().detail));
        }
    }
    static_cast<void>(calls_.waitUntilSafeToReap(std::chrono::seconds(3)));
    static_cast<void>(calls_.reap());
    static_cast<void>(logger_.info(
        "call", "Shutdown de chamadas concluído; objetos vivos="
            + std::to_string(sip::SipCall::liveCount())));

    if (account_ != nullptr) {
        account_->shutdownAccount();
        account_.reset();
    }
    audioDevices_.reset();
    if (endpoint_ != nullptr) {
        const auto destroyed = endpoint_->destroy();
        if (!destroyed) {
            static_cast<void>(logger_.log(
                logging::LogLevel::Error,
                "pjsip",
                destroyed.error().message,
                destroyed.error().detail));
        }
        endpoint_.reset();
    }
    config_.reset();
    consoleSink_.reset();
    if (!logger_.flush()) {
        std::cerr << "Aviso [LOG_FLUSH]: não foi possível descarregar todos os logs.\n";
    }
}

} // namespace polphone::app
