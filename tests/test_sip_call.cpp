/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "sip/SipCall.h"

#include <doctest/doctest.h>

TEST_SUITE("call") {
    TEST_CASE("normaliza somente os formatos definidos pela arquitetura")
    {
        const auto extension = polphone::sip::normalizeDestination(" 1002 ", "pbx.local");
        REQUIRE(extension);
        CHECK(extension.value() == "sip:1002@pbx.local");

        const auto feature = polphone::sip::normalizeDestination("*43", "pbx.local");
        REQUIRE(feature);
        CHECK(feature.value() == "sip:*43@pbx.local");

        const auto complete = polphone::sip::normalizeDestination(
            "sips:alice@example.test", "ignored.local");
        REQUIRE(complete);
        CHECK(complete.value() == "sips:alice@example.test");

        const auto literal = polphone::sip::normalizeDestination(
            "SIP:Alice@Example.test", "ignored.local");
        REQUIRE(literal);
        CHECK(literal.value() == "SIP:Alice@Example.test");
    }

    TEST_CASE("rejeita heurísticas e domínio ausente")
    {
        CHECK_FALSE(polphone::sip::normalizeDestination("alice", "pbx.local"));
        CHECK_FALSE(polphone::sip::normalizeDestination("1002", ""));
        CHECK_FALSE(polphone::sip::normalizeDestination("", "pbx.local"));
        CHECK_FALSE(polphone::sip::normalizeDestination("1002 1003", "pbx.local"));
    }

    TEST_CASE("mapeia estados PJSIP")
    {
        CHECK(polphone::sip::callStateFromPjsip(PJSIP_INV_STATE_CALLING)
              == polphone::app::CallState::Calling);
        CHECK(polphone::sip::callStateFromPjsip(PJSIP_INV_STATE_INCOMING)
              == polphone::app::CallState::Incoming);
        CHECK(polphone::sip::callStateFromPjsip(PJSIP_INV_STATE_EARLY)
              == polphone::app::CallState::Early);
        CHECK(polphone::sip::callStateFromPjsip(PJSIP_INV_STATE_CONFIRMED)
              == polphone::app::CallState::Confirmed);
        CHECK(polphone::sip::callStateFromPjsip(PJSIP_INV_STATE_CONNECTING)
              == polphone::app::CallState::Connecting);
        CHECK(polphone::sip::callStateFromPjsip(PJSIP_INV_STATE_DISCONNECTED)
              == polphone::app::CallState::Disconnected);
        CHECK(polphone::sip::callStateFromPjsip(PJSIP_INV_STATE_NULL)
              == polphone::app::CallState::Idle);
    }
}
