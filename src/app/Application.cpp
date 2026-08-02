/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "app/Application.h"

#include "config/ConfigLoader.h"
#include "config/ConfigValidator.h"
#include "logging/Logger.h"

#include <algorithm>
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
    if (!logger_.addSink(
            std::make_shared<logging::ConsoleLogSink>(std::cout),
            logging::logLevelFromNumber(consoleLevelNumber))) {
        return failInitialization(util::Error{
            util::ErrorCode::Runtime,
            "Falha ao inicializar o console de log; verifique a saída padrão.",
            "Logger::addSink"});
    }

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
            account_ = std::make_unique<sip::SipAccount>(state_, events_, logger_);
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
        static_cast<void>(logger_.info(
            "app", "Selftest do endpoint, transporte e codecs concluído com sucesso."));
    }
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

void Application::shutdown() noexcept
{
    if (shutdownStarted_) return;
    shutdownStarted_ = true;
    initialized_ = false;

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
    if (!logger_.flush()) {
        std::cerr << "Aviso [LOG_FLUSH]: não foi possível descarregar todos os logs.\n";
    }
}

} // namespace polphone::app
