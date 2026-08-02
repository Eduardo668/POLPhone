/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "dtmf/DtmfMethod.h"
#include "dtmf/DtmfPlan.h"
#include "dtmf/DtmfSender.h"
#include "app/AppState.h"
#include "app/EventQueue.h"
#include "logging/Logger.h"
#include "sip/SipCall.h"
#include "sip/CallRegistry.h"

#include <doctest/doctest.h>

TEST_SUITE("dtmf-plan") {
    TEST_CASE("expande dígitos e pausa preservando temporização")
    {
        const auto plan = polphone::dtmf::DtmfPlan::build("1,2#A", 160U, 100U);
        REQUIRE(plan);
        REQUIRE(plan.value().size() == 5U);
        CHECK(plan.value()[0].digit == '1');
        CHECK(plan.value()[0].onMs == 160U);
        CHECK(plan.value()[0].offMs == 100U);
        CHECK(plan.value()[1].kind == polphone::dtmf::DtmfPlanStep::Kind::Pause);
        CHECK(plan.value()[1].pauseMs == 500U);
        CHECK(plan.value()[4].digit == 'A');
    }

    TEST_CASE("aceita o alfabeto DTMF completo")
    {
        CHECK(polphone::dtmf::DtmfPlan::build("0123456789*#ABCD", 40U, 20U));
    }

    TEST_CASE("rejeita caracteres e sequências sem dígito")
    {
        CHECK_FALSE(polphone::dtmf::DtmfPlan::build("", 160U, 100U));
        CHECK_FALSE(polphone::dtmf::DtmfPlan::build(",,,", 160U, 100U));
        CHECK_FALSE(polphone::dtmf::DtmfPlan::build("1E", 160U, 100U));
        CHECK_FALSE(polphone::dtmf::DtmfPlan::build("1a", 160U, 100U));
        CHECK_FALSE(polphone::dtmf::DtmfPlan::build("1 2", 160U, 100U));
    }

    TEST_CASE("valida limites de duração e intervalo")
    {
        CHECK(polphone::dtmf::DtmfPlan::build("5", 40U, 20U));
        CHECK(polphone::dtmf::DtmfPlan::build("5", 2000U, 2000U));
        CHECK_FALSE(polphone::dtmf::DtmfPlan::build("5", 39U, 100U));
        CHECK_FALSE(polphone::dtmf::DtmfPlan::build("5", 2001U, 100U));
        CHECK_FALSE(polphone::dtmf::DtmfPlan::build("5", 160U, 19U));
        CHECK_FALSE(polphone::dtmf::DtmfPlan::build("5", 160U, 2001U));
    }

    TEST_CASE("normaliza nomes de método sem criar fallback")
    {
        CHECK(polphone::dtmf::parseMethod("rfc4733")
              == polphone::dtmf::DtmfMethod::Rfc4733);
        CHECK(polphone::dtmf::parseMethod("RFC2833")
              == polphone::dtmf::DtmfMethod::Rfc4733);
        CHECK(polphone::dtmf::parseMethod("inband")
              == polphone::dtmf::DtmfMethod::Inband);
        CHECK(polphone::dtmf::parseMethod("info")
              == polphone::dtmf::DtmfMethod::SipInfo);
        CHECK_FALSE(polphone::dtmf::parseMethod("auto"));
    }

    TEST_CASE("sender recusa envio sem chamada e sempre libera inFlight")
    {
        polphone::sip::CallRegistry calls;
        polphone::app::AppState state;
        polphone::app::EventQueue events;
        polphone::logging::Logger logger;
        polphone::dtmf::DtmfSender sender(calls, state, events, logger);

        polphone::config::DtmfConfig config;
        REQUIRE(sender.configure(config));
        CHECK(sender.settings().defaultMethod
              == polphone::dtmf::DtmfMethod::Rfc4733);

        const auto result = sender.send(polphone::dtmf::DtmfRequest{
            "5", polphone::dtmf::DtmfMethod::Rfc4733, 160U, 100U, -10});
        CHECK_FALSE(result);
        CHECK(result.error().message.find("Sem chamada ativa") != std::string::npos);
        CHECK_FALSE(sender.inFlight());
    }

    TEST_CASE("sender não substitui silenciosamente método ainda indisponível")
    {
        polphone::sip::CallRegistry calls;
        polphone::app::AppState state;
        polphone::app::EventQueue events;
        polphone::logging::Logger logger;
        polphone::dtmf::DtmfSender sender(calls, state, events, logger);

        const auto result = sender.send(polphone::dtmf::DtmfRequest{
            "5", polphone::dtmf::DtmfMethod::SipInfo, 160U, 100U, -10});
        CHECK_FALSE(result);
        CHECK(result.error().detail == "info");
        CHECK_FALSE(sender.inFlight());
    }
}
