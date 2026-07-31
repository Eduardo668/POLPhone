/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

// O PJSIP deve vir antes dos headers do Windows para evitar conflito de Winsock.
#include <pjsua2.hpp>

#include "util/Strings.h"

#include <exception>
#include <iostream>
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
    std::cout << "Uso: polphone.exe [--version | --selftest]\n";
}

int runSelftest()
{
    pj::Endpoint endpoint;
    bool created = false;
    try {
        endpoint.libCreate();
        created = true;
        const pj::EpConfig config;
        endpoint.libInit(config);
        endpoint.libDestroy();
        created = false;
        std::cout << "Selftest PJSUA2 concluído com sucesso.\n";
        return 0;
    } catch (const pj::Error& error) {
        if (created) {
            try { endpoint.libDestroy(); } catch (...) {}
        }
        std::cerr << "Falha no selftest PJSUA2: status=" << error.status
                  << " title=" << error.title << " reason=" << error.reason << "\n";
        return 2;
    } catch (const std::exception& error) {
        if (created) {
            try { endpoint.libDestroy(); } catch (...) {}
        }
        std::cerr << "Falha no selftest: " << error.what() << "\n";
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
    if (argc != 2) {
        std::cerr << "Erro: informe apenas uma opção por execução.\n";
        printUsage();
        return 1;
    }
    const std::string argument = argv[1];
    if (argument == "--version") {
        printVersion();
        return 0;
    }
    if (argument == "--selftest") {
        printVersion();
        return runSelftest();
    }
    std::cerr << "Opção desconhecida: " << argument << "\n";
    printUsage();
    return 1;
}
