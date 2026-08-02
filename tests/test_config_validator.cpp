/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "config/ConfigValidator.h"

#include <doctest/doctest.h>

#include <string>

using polphone::config::AppConfig;
using polphone::config::ConfigValidator;
using polphone::util::ErrorCode;

namespace {

AppConfig validConfig()
{
    AppConfig config;
    config.sip.idUri = "sip:test@example.invalid";
    config.sip.registrarUri = "sip:example.invalid:5060";
    config.sip.username = "test";
    config.sip.password = "T06_ONLY_SYNTHETIC_SECRET";
    config.sip.domain = "example.invalid";
    return config;
}

void checkInvalid(const AppConfig& config, const std::string& field)
{
    const auto result = ConfigValidator::validate(config);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ErrorCode::Validation);
    CHECK(result.error().detail == field);
}

} // namespace

TEST_SUITE("config") {
    TEST_CASE("configuração completa válida é aceita")
    {
        CHECK(ConfigValidator::validate(validConfig()));
    }

    TEST_CASE("URIs SIP e campos obrigatórios são validados")
    {
        auto config = validConfig();
        config.sip.idUri = "http://example.invalid";
        checkInvalid(config, "sip.idUri");

        config = validConfig();
        config.sip.registrarUri = "example.invalid";
        checkInvalid(config, "sip.registrarUri");

        config = validConfig();
        config.sip.proxyUri = "udp://example.invalid";
        checkInvalid(config, "sip.proxyUri");

        config = validConfig();
        config.sip.password.clear();
        checkInvalid(config, "sip.password");
    }

    TEST_CASE("porta local aceita limites e rejeita valores externos")
    {
        auto config = validConfig();
        config.network.localPort = 0;
        CHECK(ConfigValidator::validate(config));
        config.network.localPort = 65535;
        CHECK(ConfigValidator::validate(config));
        config.network.localPort = -1;
        checkInvalid(config, "network.localPort");
        config.network.localPort = 65536;
        checkInvalid(config, "network.localPort");
    }

    TEST_CASE("somente transporte UDP é aceito")
    {
        auto config = validConfig();
        config.network.transport = "tcp";
        checkInvalid(config, "network.transport");
    }

    TEST_CASE("duração DTMF aceita limites e rejeita valores externos")
    {
        auto config = validConfig();
        config.dtmf.durationMs = 40;
        CHECK(ConfigValidator::validate(config));
        config.dtmf.durationMs = 2000;
        CHECK(ConfigValidator::validate(config));
        config.dtmf.durationMs = 39;
        checkInvalid(config, "dtmf.durationMs");
        config.dtmf.durationMs = 2001;
        checkInvalid(config, "dtmf.durationMs");
    }

    TEST_CASE("intervalo DTMF aceita limites e rejeita valores externos")
    {
        auto config = validConfig();
        config.dtmf.gapMs = 20;
        CHECK(ConfigValidator::validate(config));
        config.dtmf.gapMs = 2000;
        CHECK(ConfigValidator::validate(config));
        config.dtmf.gapMs = 19;
        checkInvalid(config, "dtmf.gapMs");
        config.dtmf.gapMs = 2001;
        checkInvalid(config, "dtmf.gapMs");
    }

    TEST_CASE("volume DTMF aceita limites e rejeita valores externos")
    {
        auto config = validConfig();
        config.dtmf.volumeDbm0 = -30;
        CHECK(ConfigValidator::validate(config));
        config.dtmf.volumeDbm0 = 0;
        CHECK(ConfigValidator::validate(config));
        config.dtmf.volumeDbm0 = -31;
        checkInvalid(config, "dtmf.volumeDbm0");
        config.dtmf.volumeDbm0 = 1;
        checkInvalid(config, "dtmf.volumeDbm0");
    }

    TEST_CASE("método DTMF deve ser conhecido")
    {
        auto config = validConfig();
        config.dtmf.defaultMethod = "automatic";
        checkInvalid(config, "dtmf.defaultMethod");
    }

    TEST_CASE("prioridade de codec aceita 0 e 255")
    {
        auto config = validConfig();
        config.codecs.priority["test/8000/1"] = 0;
        CHECK(ConfigValidator::validate(config));
        config.codecs.priority["test/8000/1"] = 255;
        CHECK(ConfigValidator::validate(config));
        config.codecs.priority["test/8000/1"] = -1;
        checkInvalid(config, "codecs.priority.test/8000/1");
        config.codecs.priority["test/8000/1"] = 256;
        checkInvalid(config, "codecs.priority.test/8000/1");
    }

    TEST_CASE("níveis de log aceitam 0 a 6")
    {
        auto config = validConfig();
        config.logging.consoleLevel = 0;
        config.logging.fileLevel = 6;
        CHECK(ConfigValidator::validate(config));
        config.logging.consoleLevel = -1;
        checkInvalid(config, "logging.consoleLevel");
        config = validConfig();
        config.logging.fileLevel = 7;
        checkInvalid(config, "logging.fileLevel");
    }

    TEST_CASE("parâmetros essenciais de áudio são validados")
    {
        auto config = validConfig();
        config.audio.channelCount = 2;
        checkInvalid(config, "audio.channelCount");
        config = validConfig();
        config.audio.quality = 7;
        checkInvalid(config, "audio.quality");
        config = validConfig();
        config.audio.quality = 10;
        CHECK(ConfigValidator::validate(config));
    }
}
