/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

// O PJSIP deve vir antes dos headers do Windows para evitar conflito de Winsock.
#include <pjsua2.hpp>

#include "config/ConfigLoader.h"
#include "config/ConfigValidator.h"
#include "logging/Logger.h"
#include "logging/PjLogWriter.h"
#include "util/Result.h"
#include "util/Strings.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {
constexpr const char* kPolphoneVersion = "0.1.0";

void printVersion()
{
    std::cout << "POLPhone " << kPolphoneVersion << "\n"
              << "PJSIP " << pj_get_version() << "\n";
}

void printUsage()
{
    std::cout << "Uso: polphone.exe [--version]\n"
              << "     polphone.exe [--config <caminho>] [--selftest] [--log-level 0..6]\n";
}

std::string_view configErrorCode(polphone::util::ErrorCode code)
{
    using polphone::util::ErrorCode;
    switch (code) {
    case ErrorCode::NotFound: return "CONFIG_NOT_FOUND";
    case ErrorCode::Io: return "CONFIG_IO";
    case ErrorCode::Parse: return "CONFIG_PARSE";
    case ErrorCode::Validation: return "CONFIG_VALIDATION";
    default: return "CONFIG_ERROR";
    }
}

void printConfigError(const polphone::util::Error& error)
{
    std::cerr << "Erro [" << configErrorCode(error.code) << "]: " << error.message << '\n';
    if (!error.detail.empty()) {
        std::cerr << "Caminho/campo: " << error.detail << '\n';
    }
}

polphone::util::Result<polphone::config::AppConfig> loadConfiguration(
    const std::filesystem::path& path,
    polphone::config::ConfigLoader::Warnings& warnings)
{
    auto loaded = polphone::config::ConfigLoader::load(path, &warnings);
    if (!loaded) {
        return polphone::util::Result<polphone::config::AppConfig>::failure(loaded.error());
    }
    const auto validated = polphone::config::ConfigValidator::validate(loaded.value());
    if (!validated) {
        return polphone::util::Result<polphone::config::AppConfig>::failure(validated.error());
    }
    return polphone::util::Result<polphone::config::AppConfig>::success(
        std::move(loaded).value());
}

int validateConfigurationOnly(const std::filesystem::path& path)
{
    polphone::config::ConfigLoader::Warnings warnings;
    const auto loaded = loadConfiguration(path, warnings);
    if (!loaded) {
        printConfigError(loaded.error());
        return 1;
    }
    for (const auto& warning : warnings) {
        std::cerr << "Aviso [CONFIG_UNKNOWN_FIELD]: " << warning << '\n';
    }
    std::cout << "Configuração válida: " << path.u8string() << '\n';
    return 0;
}

int runSelftest(const std::filesystem::path& configPath,
                std::optional<int> requestedLogLevel)
{
    using polphone::config::ConfigLoader;
    using polphone::logging::ConsoleLogSink;
    using polphone::logging::LogLevel;
    using polphone::logging::Logger;
    using polphone::logging::PjLogWriter;

    Logger logger;
    ConfigLoader::Warnings warnings;
    const auto loaded = loadConfiguration(configPath, warnings);
    if (!loaded) {
        printConfigError(loaded.error());
        return 1;
    }
    const auto& appConfig = loaded.value();
    const int consoleLevelNumber = requestedLogLevel.value_or(appConfig.logging.consoleLevel);
    const LogLevel consoleLevel = polphone::logging::logLevelFromNumber(consoleLevelNumber);
    if (!logger.addSink(std::make_shared<ConsoleLogSink>(std::cout), consoleLevel)) {
        std::cerr << "Falha ao inicializar o destino de log do console.\n";
        return 3;
    }
    const auto maxFileBytes = static_cast<std::uintmax_t>(appConfig.logging.maxFileMB)
        * 1024U * 1024U;
    const LogLevel fileLevel =
        polphone::logging::logLevelFromNumber(appConfig.logging.fileLevel);
    const auto logFile = logger.enableFile(
        appConfig.logging.directory, fileLevel, maxFileBytes);
    if (!logFile) {
        static_cast<void>(logger.warning(
            "logging", "Arquivo de log indisponível; continuando somente no console."));
    }

    for (const auto& warning : warnings) {
        static_cast<void>(logger.warning("config", warning));
    }
    static_cast<void>(logger.info("config", appConfig.redactedDump()));

    static_cast<void>(logger.info("app", "Selftest de logging iniciado."));
    static_cast<void>(logger.debug(
        "app",
        "Redaction ativa: password=T05_SELFTEST_VALUE destino=999999999999."));

    // Ordem exigida pela tag 2.17 (ADR-021): Logger -> Endpoint -> writer
    // transferido no libInit -> libDestroy apaga writer -> Logger.
    pj::Endpoint endpoint;
    bool created = false;
    try {
        endpoint.libCreate();
        created = true;
        pj::EpConfig config;
        const int pjsipLogLevel = std::max(consoleLevelNumber, appConfig.logging.fileLevel);
        config.logConfig.level = pjsipLogLevel;
        // Na tag 2.17, consoleLevel também limita o callback customizado.
        // Com writer instalado, o callback substitui a saída nativa (ADR-022).
        config.logConfig.consoleLevel = pjsipLogLevel;
        config.logConfig.msgLogging = appConfig.logging.sipMessageTrace ? 1 : 0;
        config.logConfig.writer = new PjLogWriter(logger);
        endpoint.libInit(config);
        endpoint.libStart();
        endpoint.libDestroy();
        created = false;
        static_cast<void>(logger.info("app", "Selftest PJSUA2 concluído com sucesso."));
        if (!logger.flush()) {
            std::cerr << "Aviso: não foi possível descarregar todos os destinos de log.\n";
        }
        return 0;
    } catch (const pj::Error& error) {
        if (created) {
            try { endpoint.libDestroy(); } catch (...) {}
        }
        static_cast<void>(logger.log(
            LogLevel::Error,
            "pjsip",
            "Falha no selftest PJSUA2.",
            "status=" + std::to_string(error.status) + " title=" + error.title
                + " reason=" + error.reason));
        return 2;
    } catch (const std::exception& error) {
        if (created) {
            try { endpoint.libDestroy(); } catch (...) {}
        }
        static_cast<void>(logger.log(
            LogLevel::Error, "app", "Falha no selftest.", error.what()));
        return 3;
    } catch (...) {
        if (created) {
            try { endpoint.libDestroy(); } catch (...) {}
        }
        static_cast<void>(logger.error("app", "Falha desconhecida no selftest."));
        return 3;
    }
}
} // namespace

int main(int argc, char* argv[])
{
    if (!polphone::util::toUtf8Console()) {
        std::cerr << "Aviso: não foi possível configurar o console para UTF-8.\n";
    }
    if (argc == 1) {
        printVersion();
        printUsage();
        return 0;
    }
    const std::string firstArgument = argv[1];
    if (firstArgument == "--version") {
        if (argc != 2) {
            std::cerr << "Erro: --version não aceita argumentos adicionais.\n";
            return 1;
        }
        printVersion();
        return 0;
    }
    std::filesystem::path configPath = "config/polphone.config.json";
    bool configWasSpecified = false;
    bool selftest = false;
    std::optional<int> logLevel;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--selftest" && !selftest) {
            selftest = true;
            continue;
        }
        if (argument == "--config" && !configWasSpecified && index + 1 < argc
            && std::string_view(argv[index + 1]).find("--") != 0U) {
            configPath = argv[++index];
            configWasSpecified = true;
            continue;
        }
        if (argument == "--log-level" && !logLevel.has_value() && index + 1 < argc) {
            const std::string levelText = argv[++index];
            if (levelText.size() != 1U || levelText[0] < '0' || levelText[0] > '6') {
                std::cerr << "Erro [CONFIG_ARGUMENT]: --log-level deve estar entre 0 e 6.\n";
                return 1;
            }
            logLevel = levelText[0] - '0';
            continue;
        }
        std::cerr << "Erro [CONFIG_ARGUMENT]: argumento inválido ou repetido: " << argument
                  << ". Revise a linha de comando.\n";
        printUsage();
        return 1;
    }
    if (logLevel.has_value() && !selftest) {
        std::cerr << "Erro [CONFIG_ARGUMENT]: --log-level requer --selftest nesta etapa.\n";
        return 1;
    }
    if (selftest) {
        printVersion();
        return runSelftest(configPath, logLevel);
    }
    if (configWasSpecified) {
        return validateConfigurationOnly(configPath);
    }

    std::cerr << "Erro [CONFIG_ARGUMENT]: nenhuma ação reconhecida. Use --selftest ou --config.\n";
    printUsage();
    return 1;
}
