/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "app/CommandParser.h"

#include <doctest/doctest.h>

using polphone::app::CommandParser;
using polphone::app::CommandVerb;

TEST_SUITE("parser") {
    TEST_CASE("aceita comandos simples e ignora espaços externos")
    {
        for (const auto& input : {"help", " status ", "devices", "answer", "hangup", "codecs", "quit"}) {
            CHECK(CommandParser::parse(input));
        }
        CHECK_FALSE(CommandParser::parse(""));
        CHECK_FALSE(CommandParser::parse("help agora"));
        CHECK_FALSE(CommandParser::parse("desconhecido"));
    }

    TEST_CASE("parseia controle SIP e dispositivos com tipos")
    {
        const auto setdev = CommandParser::parse("setdev in 12");
        REQUIRE(setdev);
        CHECK(setdev.value().verb == CommandVerb::SetDevice);
        CHECK(setdev.value().deviceDirection == polphone::app::DeviceDirection::Capture);
        CHECK(setdev.value().value == 12);

        const auto registration = CommandParser::parse("reg OFF");
        REQUIRE(registration);
        CHECK(registration.value().enabled == false);

        const auto call = CommandParser::parse("call sips:alice@example.test");
        REQUIRE(call);
        CHECK(call.value().text == "sips:alice@example.test");

        CHECK_FALSE(CommandParser::parse("setdev side 1"));
        CHECK_FALSE(CommandParser::parse("setdev out -1"));
        CHECK_FALSE(CommandParser::parse("reg maybe"));
        CHECK_FALSE(CommandParser::parse("call"));
    }

    TEST_CASE("parseia todas as opções DTMF independentemente da ordem")
    {
        const auto parsed = CommandParser::parse(
            "dtmf 12# --gap 90 --method info --duration 250");
        REQUIRE(parsed);
        CHECK(parsed.value().verb == CommandVerb::Dtmf);
        CHECK(parsed.value().text == "12#");
        CHECK(parsed.value().method == polphone::dtmf::DtmfMethod::SipInfo);
        CHECK(parsed.value().durationMs == 250);
        CHECK(parsed.value().gapMs == 90);

        CHECK(CommandParser::parse("dtmfmode rfc4733"));
        CHECK(CommandParser::parse("dtmfmode rfc2833"));
        CHECK(CommandParser::parse("dtmfcfg duration 160"));
        CHECK(CommandParser::parse("dtmfcfg gap 100"));
        CHECK(CommandParser::parse("dtmfcfg volume -10"));
        CHECK(CommandParser::parse("dtmf 5 --duration 40 --gap 20"));
        CHECK(CommandParser::parse("dtmf 5 --duration 2000 --gap 2000"));
        CHECK(CommandParser::parse("dtmfcfg duration 40"));
        CHECK(CommandParser::parse("dtmfcfg duration 2000"));
        CHECK(CommandParser::parse("dtmfcfg gap 20"));
        CHECK(CommandParser::parse("dtmfcfg gap 2000"));
        CHECK(CommandParser::parse("dtmfcfg volume -30"));
        CHECK(CommandParser::parse("dtmfcfg volume 0"));
    }

    TEST_CASE("rejeita flags DTMF malformadas ou duplicadas")
    {
        CHECK_FALSE(CommandParser::parse("dtmf"));
        CHECK_FALSE(CommandParser::parse("dtmf 1 --method"));
        CHECK_FALSE(CommandParser::parse("dtmf 1 --method auto"));
        CHECK_FALSE(CommandParser::parse("dtmf 1 --duration zero"));
        CHECK_FALSE(CommandParser::parse("dtmf 1 --gap -1"));
        CHECK_FALSE(CommandParser::parse("dtmf 1 --gap 20 --gap 30"));
        CHECK_FALSE(CommandParser::parse("dtmf 1 --fallback info"));
        CHECK_FALSE(CommandParser::parse("dtmfmode auto"));
        CHECK_FALSE(CommandParser::parse("dtmfcfg unknown 1"));
        CHECK_FALSE(CommandParser::parse("dtmf 1 --duration 39"));
        CHECK_FALSE(CommandParser::parse("dtmf 1 --duration 2001"));
        CHECK_FALSE(CommandParser::parse("dtmf 1 --gap 19"));
        CHECK_FALSE(CommandParser::parse("dtmf 1 --gap 2001"));
        CHECK_FALSE(CommandParser::parse("dtmfcfg duration 39"));
        CHECK_FALSE(CommandParser::parse("dtmfcfg duration 2001"));
        CHECK_FALSE(CommandParser::parse("dtmfcfg gap 19"));
        CHECK_FALSE(CommandParser::parse("dtmfcfg gap 2001"));
        CHECK_FALSE(CommandParser::parse("dtmfcfg volume -31"));
        CHECK_FALSE(CommandParser::parse("dtmfcfg volume 1"));
    }

    TEST_CASE("valida nível de log")
    {
        const auto valid = CommandParser::parse("loglevel 6");
        REQUIRE(valid);
        CHECK(valid.value().value == 6);
        CHECK_FALSE(CommandParser::parse("loglevel 7"));
        CHECK_FALSE(CommandParser::parse("loglevel debug"));
    }
}
