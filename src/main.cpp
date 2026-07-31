/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

// O PJSIP deve vir antes dos headers do Windows para evitar conflito de Winsock.
#include <pjsua2.hpp>

#include "logging/Logger.h"
#include "logging/PjLogWriter.h"
#include "util/Strings.h"

#include <exception>
#include <iostream>
#include <memory>
#include <string>

namespace {
constexpr const char* kPolphoneVersion = "0.1.0";

void printVersion()
{
    std::cout << "POLPhone " << kPolphoneVersion << "\n"
              << "PJSIP " << pj_get_version() << "\n";
}

void printUsage()
{
    std::cout << "Uso: polphone.exe [--version | --selftest [--log-level 0..6]]\n";
}

int runSelftest(int requestedLogLevel)
{
    using polphone::logging::ConsoleLogSink;
    using polphone::logging::LogLevel;
    using polphone::logging::Logger;
    using polphone::logging::PjLogWriter;

    Logger logger;
    const LogLevel consoleLevel = polphone::logging::logLevelFromNumber(requestedLogLevel);
    if (!logger.addSink(std::make_shared<ConsoleLogSink>(std::cout), consoleLevel)) {
        std::cerr << "Falha ao inicializar o destino de log do console.\n";
        return 3;
    }
    const auto logFile = logger.enableFile("logs", LogLevel::Debug);
    if (!logFile) {
        static_cast<void>(logger.warning(
            "logging", "Arquivo de log indisponível; continuando somente no console."));
    }

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
        config.logConfig.level = 5;
        // Na tag 2.17, consoleLevel também limita o callback customizado.
        // Com writer instalado, o callback substitui a saída nativa (ADR-022).
        config.logConfig.consoleLevel = 5;
        config.logConfig.msgLogging = 1;
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
        return 2;
    } catch (...) {
        if (created) {
            try { endpoint.libDestroy(); } catch (...) {}
        }
        static_cast<void>(logger.error("app", "Falha desconhecida no selftest."));
        return 2;
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
    const std::string argument = argv[1];
    if (argument == "--version") {
        if (argc != 2) {
            std::cerr << "Erro: --version não aceita argumentos adicionais.\n";
            return 1;
        }
        printVersion();
        return 0;
    }
    if (argument == "--selftest") {
        int logLevel = 4;
        if (argc == 4 && std::string(argv[2]) == "--log-level") {
            const std::string levelText = argv[3];
            if (levelText.size() != 1U || levelText[0] < '0' || levelText[0] > '6') {
                std::cerr << "Erro: --log-level deve estar entre 0 e 6.\n";
                return 1;
            }
            logLevel = levelText[0] - '0';
        } else if (argc != 2) {
            std::cerr << "Erro: argumentos inválidos para --selftest.\n";
            printUsage();
            return 1;
        }
        printVersion();
        return runSelftest(logLevel);
    }
    std::cerr << "Opção desconhecida: " << argument << "\n";
    printUsage();
    return 1;
}
