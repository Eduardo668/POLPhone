/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "sip/PjErrors.h"

#include <doctest/doctest.h>

#include <string>

namespace {

int throwSyntheticPjError()
{
    throw pj::Error(
        PJ_EINVAL,
        "syntheticOperation",
        "synthetic reason",
        "C:\\private\\path\\synthetic.cpp",
        42);
}

} // namespace

TEST_SUITE("sip") {
    TEST_CASE("describe inclui diagnóstico sem caminho absoluto")
    {
        const pj::Error error(
            PJ_EINVAL,
            "syntheticOperation",
            "synthetic reason",
            "C:\\private\\path\\synthetic.cpp",
            42);
        const std::string description = polphone::sip::describe(error);
        CHECK(description.find("status=") != std::string::npos);
        CHECK(description.find("syntheticOperation") != std::string::npos);
        CHECK(description.find("synthetic reason") != std::string::npos);
        CHECK(description.find("synthetic.cpp:42") != std::string::npos);
        CHECK(description.find("private") == std::string::npos);
    }

    TEST_CASE("POLPHONE_PJ_TRY converte retorno e erro PJSIP em Result")
    {
        const auto success = POLPHONE_PJ_TRY(21 + 21);
        REQUIRE(success);
        CHECK(success.value() == 42);

        const auto failure = POLPHONE_PJ_TRY(throwSyntheticPjError());
        REQUIRE_FALSE(failure);
        CHECK(failure.error().code == polphone::util::ErrorCode::Pjsip);
        CHECK(failure.error().detail.find("syntheticOperation") != std::string::npos);
    }

    TEST_CASE("POLPHONE_PJ_TRY suporta operação void")
    {
        bool called = false;
        const auto result = POLPHONE_PJ_TRY(static_cast<void>(called = true));
        CHECK(result);
        CHECK(called);
    }
}
