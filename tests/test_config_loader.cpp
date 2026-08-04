/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "config/ConfigLoader.h"
#include "config/ConfigValidator.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

using polphone::config::ConfigLoader;
using polphone::util::ErrorCode;

namespace {
constexpr const char* kSyntheticPassword = "T06_ONLY_SYNTHETIC_SECRET";
}

TEST_SUITE("config") {
    TEST_CASE("arquivo de exemplo versionado carrega e valida")
    {
        const std::filesystem::path path =
            std::filesystem::path(POLPHONE_SOURCE_DIR)
            / "config" / "polphone.config.example.json";
        const auto loaded = ConfigLoader::load(path);
        REQUIRE(loaded);
        CHECK(polphone::config::ConfigValidator::validate(loaded.value()));
    }

    TEST_CASE("objeto vazio aplica todos os defaults")
    {
        const auto loaded = ConfigLoader::parse("{}");
        REQUIRE(loaded);
        const auto& config = loaded.value();
        CHECK(config.sip.realm == "*");
        CHECK(config.sip.regTimeoutSec == 300);
        CHECK(config.network.localPort == 0);
        CHECK(config.network.transport == "udp");
        CHECK(config.audio.clockRate == 8000);
        CHECK(config.codecs.priority.at("PCMU/8000/1") == 254);
        CHECK(config.dtmf.defaultMethod == "rfc4733");
        CHECK(config.dtmf.durationMs == 160);
        CHECK(config.logging.consoleLevel == 4);
        CHECK(config.logging.maxFileMB == 50);
        CHECK(config.behavior.ringtoneEnabled);
        CHECK_FALSE(config.behavior.topmostOnIncomingCall);
    }

    TEST_CASE("campos informados substituem defaults sem apagar prioridades omitidas")
    {
        const auto loaded = ConfigLoader::parse(R"({
            "sip": {"idUri":"sip:test@example.invalid","displayName":"Operador",
                    "authUsername":"digest-user","password":"T06_ONLY_SYNTHETIC_SECRET"},
            "network": {"localPort":5060},
            "audio": {"captureDevice":"#3","noVad":false},
            "codecs": {"priority":{"PCMU/8000/1":200}},
            "dtmf": {"defaultMethod":"info","volumeDbm0":-20},
            "logging": {"directory":"diagnostic-logs","sipMessageTrace":false}
            ,"behavior": {"ringtoneEnabled":false,"topmostOnIncomingCall":true}
        })");
        REQUIRE(loaded);
        const auto& config = loaded.value();
        CHECK(config.sip.idUri == "sip:test@example.invalid");
        CHECK(config.sip.displayName == "Operador");
        CHECK(config.sip.authUsername == "digest-user");
        CHECK(config.sip.password == kSyntheticPassword);
        CHECK(config.network.localPort == 5060);
        CHECK(config.audio.captureDevice == "#3");
        CHECK_FALSE(config.audio.noVad);
        CHECK(config.codecs.priority.at("PCMU/8000/1") == 200);
        CHECK(config.codecs.priority.at("PCMA/8000/1") == 253);
        CHECK(config.dtmf.defaultMethod == "info");
        CHECK(config.dtmf.volumeDbm0 == -20);
        CHECK(config.logging.directory == "diagnostic-logs");
        CHECK_FALSE(config.logging.sipMessageTrace);
        CHECK_FALSE(config.behavior.ringtoneEnabled);
        CHECK(config.behavior.topmostOnIncomingCall);
    }

    TEST_CASE("tipo errado aponta o caminho completo do campo")
    {
        const auto loaded = ConfigLoader::parse(R"({"dtmf":{"durationMs":"160"}})");
        REQUIRE_FALSE(loaded);
        CHECK(loaded.error().code == ErrorCode::Parse);
        CHECK(loaded.error().detail == "dtmf.durationMs");
    }

    TEST_CASE("seção com tipo errado aponta a seção")
    {
        const auto loaded = ConfigLoader::parse(R"({"audio":[]})");
        REQUIRE_FALSE(loaded);
        CHECK(loaded.error().detail == "audio");
    }

    TEST_CASE("JSON malformado informa a posição aproximada")
    {
        const auto loaded = ConfigLoader::parse(R"({"sip":)");
        REQUIRE_FALSE(loaded);
        CHECK(loaded.error().code == ErrorCode::Parse);
        CHECK(loaded.error().detail.find("byte") != std::string::npos);
    }

    TEST_CASE("campos desconhecidos produzem avisos e não impedem o parse")
    {
        ConfigLoader::Warnings warnings;
        const auto loaded = ConfigLoader::parse(
            R"({"futureRoot":true,"audio":{"futureAudio":42}})", &warnings);
        REQUIRE(loaded);
        REQUIRE(warnings.size() == 2U);
        const bool hasFutureAudio = warnings[0].find("futureAudio") != std::string::npos
            || warnings[1].find("futureAudio") != std::string::npos;
        const bool hasFutureRoot = warnings[0].find("futureRoot") != std::string::npos
            || warnings[1].find("futureRoot") != std::string::npos;
        CHECK(hasFutureAudio);
        CHECK(hasFutureRoot);
    }

    TEST_CASE("arquivo inexistente retorna instrução acionável")
    {
        const std::filesystem::path missing =
            std::filesystem::temp_directory_path() / "polphone-t06-does-not-exist.json";
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        const auto loaded = ConfigLoader::load(missing);
        REQUIRE_FALSE(loaded);
        CHECK(loaded.error().code == ErrorCode::NotFound);
        CHECK(loaded.error().message.find("polphone.config.example.json") != std::string::npos);
    }

    TEST_CASE("loader lê um documento JSON do disco")
    {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "polphone-t06-config-loader.json";
        {
            std::ofstream output(path, std::ios::out | std::ios::trunc);
            REQUIRE(output.is_open());
            output << R"({"network":{"localPort":5070}})";
        }
        const auto loaded = ConfigLoader::load(path);
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        REQUIRE(loaded);
        CHECK(loaded.value().network.localPort == 5070);
    }

    TEST_CASE("redactedDump nunca contém a senha")
    {
        auto loaded = ConfigLoader::parse(
            R"({"sip":{"password":"T06_ONLY_SYNTHETIC_SECRET"}})");
        REQUIRE(loaded);
        loaded.value().sip.username = kSyntheticPassword;
        const std::string dump = loaded.value().redactedDump();
        CHECK(dump.find(kSyntheticPassword) == std::string::npos);
        CHECK(dump.find(R"("password":"***")") != std::string::npos);
    }
}
