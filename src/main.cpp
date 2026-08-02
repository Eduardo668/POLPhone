/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include <pj/config.h>

#include "app/Application.h"
#include "util/Result.h"
#include "util/Strings.h"

#include <iostream>
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
              << "     polphone.exe [--config <caminho>] [--selftest | --list-devices]"
                 " [--log-level 0..6]\n";
}

std::string_view errorCodeName(polphone::util::ErrorCode code)
{
    using polphone::util::ErrorCode;
    switch (code) {
    case ErrorCode::InvalidArgument: return "ARGUMENT";
    case ErrorCode::NotFound: return "CONFIG_NOT_FOUND";
    case ErrorCode::Io: return "CONFIG_IO";
    case ErrorCode::Parse: return "CONFIG_PARSE";
    case ErrorCode::Validation: return "CONFIG_VALIDATION";
    case ErrorCode::Pjsip: return "PJSIP_INIT";
    case ErrorCode::Runtime: return "RUNTIME";
    }
    return "UNKNOWN";
}

int exitCodeFor(const polphone::util::Error& error)
{
    using polphone::util::ErrorCode;
    switch (error.code) {
    case ErrorCode::NotFound:
    case ErrorCode::Io:
    case ErrorCode::Parse:
    case ErrorCode::Validation:
    case ErrorCode::InvalidArgument:
        return 1;
    case ErrorCode::Pjsip:
        return 2;
    case ErrorCode::Runtime:
        return 3;
    }
    return 3;
}

void printError(const polphone::util::Error& error)
{
    std::cerr << "Erro [" << errorCodeName(error.code) << "]: " << error.message << '\n';
    if (!error.detail.empty()) {
        std::cerr << "Detalhe: " << error.detail << '\n';
    }
}

} // namespace

int main(int argc, char* argv[])
{
    if (!polphone::util::toUtf8Console()) {
        std::cerr << "Aviso [CONSOLE_UTF8]: não foi possível configurar UTF-8; "
                     "evite caracteres acentuados na configuração.\n";
    }
    if (argc == 1) {
        printVersion();
        printUsage();
        return 0;
    }

    const std::string firstArgument = argv[1];
    if (firstArgument == "--version") {
        if (argc != 2) {
            std::cerr << "Erro [ARGUMENT]: --version não aceita argumentos adicionais.\n";
            return 1;
        }
        printVersion();
        return 0;
    }

    polphone::app::ApplicationOptions options;
    bool configWasSpecified = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--selftest" && !options.selftest) {
            options.selftest = true;
            continue;
        }
        if (argument == "--list-devices" && !options.listDevices) {
            options.listDevices = true;
            continue;
        }
        if (argument == "--config" && !configWasSpecified && index + 1 < argc
            && std::string_view(argv[index + 1]).find("--") != 0U) {
            options.configPath = argv[++index];
            configWasSpecified = true;
            continue;
        }
        if (argument == "--log-level" && !options.consoleLogLevel.has_value()
            && index + 1 < argc) {
            const std::string levelText = argv[++index];
            if (levelText.size() != 1U || levelText[0] < '0' || levelText[0] > '6') {
                std::cerr << "Erro [ARGUMENT]: --log-level deve estar entre 0 e 6.\n";
                return 1;
            }
            options.consoleLogLevel = levelText[0] - '0';
            continue;
        }
        std::cerr << "Erro [ARGUMENT]: argumento inválido ou repetido: " << argument
                  << ". Revise a linha de comando.\n";
        printUsage();
        return 1;
    }

    if (options.selftest && options.listDevices) {
        std::cerr << "Erro [ARGUMENT]: --selftest e --list-devices são ações exclusivas.\n";
        return 1;
    }
    if (!options.selftest && !options.listDevices && !configWasSpecified) {
        std::cerr << "Erro [ARGUMENT]: nenhuma ação reconhecida. Use --selftest, --list-devices ou --config.\n";
        printUsage();
        return 1;
    }
    options.useBuiltInConfig = options.listDevices && !configWasSpecified;

    printVersion();
    polphone::app::Application application(std::move(options));
    const auto initialized = application.initialize();
    if (!initialized) {
        printError(initialized.error());
        return exitCodeFor(initialized.error());
    }
    return application.run();
}
