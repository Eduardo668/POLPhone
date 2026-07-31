/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "util/Result.h"
#include "util/Strings.h"
#include "util/Time.h"

#include <doctest/doctest.h>

#include <string>

using namespace polphone::util;

TEST_SUITE("strings") {
    TEST_CASE("trim remove somente espaços das extremidades")
    {
        CHECK(trim("").empty());
        CHECK(trim(" \t\r\n ").empty());
        CHECK(trim("  POL Phone  ") == "POL Phone");
        CHECK(trim("sem-espacos") == "sem-espacos");
    }

    TEST_CASE("split descarta vazios por padrão e pode preservá-los")
    {
        const auto compact = split("a,,b,", ',');
        REQUIRE(compact.size() == 2);
        CHECK(compact[0] == "a");
        CHECK(compact[1] == "b");

        const auto complete = split("a,,b,", ',', true);
        REQUIRE(complete.size() == 4);
        CHECK(complete[0] == "a");
        CHECK(complete[1].empty());
        CHECK(complete[2] == "b");
        CHECK(complete[3].empty());

        CHECK(split("", ',').empty());
        const auto emptyPreserved = split("", ',', true);
        REQUIRE(emptyPreserved.size() == 1);
        CHECK(emptyPreserved[0].empty());
    }

    TEST_CASE("startsWith compara prefixo de forma exata")
    {
        CHECK(startsWith("POLPhone", "POL"));
        CHECK(startsWith("POLPhone", ""));
        CHECK_FALSE(startsWith("POLPhone", "pol"));
        CHECK_FALSE(startsWith("POL", "POLPhone"));
    }

    TEST_CASE("iequals ignora caixa ASCII")
    {
        CHECK(iequals("RFC4733", "rfc4733"));
        CHECK(iequals("InBand", "INBAND"));
        CHECK_FALSE(iequals("info", "infos"));
        CHECK_FALSE(iequals("PCMA", "PCMU"));
    }

    TEST_CASE("maskMiddle preserva formato e os últimos dígitos")
    {
        CHECK(maskMiddle("", 4).empty());
        CHECK(maskMiddle("1234", 4) == "1234");
        CHECK(maskMiddle("5511987654321", 4) == "*********4321");
        CHECK(maskMiddle("+5511987654321", 4) == "+*********4321");
        CHECK(maskMiddle("ramal-123456", 4) == "ramal-**3456");
        CHECK(maskMiddle("abc", 4) == "abc");
        CHECK(maskMiddle("12-34", 0) == "**-**");
    }

    TEST_CASE("toUtf8Console configura as code pages do processo")
    {
        CHECK(toUtf8Console());
    }
}

TEST_SUITE("result") {
    TEST_CASE("Result transporta valor sem exceção")
    {
        auto result = Result<int>::success(42);
        CHECK(result.hasValue());
        CHECK_FALSE(result.hasError());
        CHECK(static_cast<bool>(result));
        CHECK(result.value() == 42);
    }

    TEST_CASE("Result transporta erro tipado com detalhe")
    {
        const auto result = Result<int>::failure(
            ErrorCode::Validation, "Valor inválido.", "dtmf.durationMs");
        CHECK(result.hasError());
        CHECK_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::Validation);
        CHECK(result.error().message == "Valor inválido.");
        CHECK(result.error().detail == "dtmf.durationMs");
    }

    TEST_CASE("erro pode ser propagado para outro tipo de Result")
    {
        const auto source = Result<int>::failure(
            ErrorCode::Parse, "JSON inválido.", "linha 3");
        const auto propagated = Result<std::string>::failure(source.error());
        CHECK(propagated.hasError());
        CHECK(propagated.error().code == ErrorCode::Parse);
        CHECK(propagated.error().message == source.error().message);
        CHECK(propagated.error().detail == source.error().detail);
    }

    TEST_CASE("Result void representa sucesso ou erro")
    {
        const auto success = Result<void>::success();
        CHECK(success.hasValue());

        const auto failure = Result<void>::failure(Error{
            ErrorCode::Io, "Falha de leitura.", "config/polphone.config.json"});
        CHECK(failure.hasError());
        CHECK(failure.error().code == ErrorCode::Io);
    }
}

TEST_SUITE("time") {
    TEST_CASE("timestamp ISO-8601 contém milissegundos e fuso")
    {
        const std::string timestamp = iso8601Now();
        REQUIRE(timestamp.size() == 29);
        CHECK(timestamp[4] == '-');
        CHECK(timestamp[7] == '-');
        CHECK(timestamp[10] == 'T');
        CHECK(timestamp[19] == '.');
        CHECK((timestamp[23] == '+' || timestamp[23] == '-'));
        CHECK(timestamp[26] == ':');
    }

    TEST_CASE("relógio monotônico não retrocede")
    {
        const auto first = monotonicMilliseconds();
        const auto second = monotonicMilliseconds();
        CHECK(second >= first);
    }
}
