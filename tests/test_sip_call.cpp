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

    TEST_CASE("preserva a porta nao padrao do registrar em destinos curtos")
    {
        const auto extension = polphone::sip::normalizeDestination(
            "600", "pbx.local", "sip:192.0.2.10:15060");
        REQUIRE(extension);
        CHECK(extension.value() == "sip:600@192.0.2.10:15060");

        const auto registrarWithParameters = polphone::sip::normalizeDestination(
            "9991", "pbx.local", "sip:lab.invalid:16060;transport=udp");
        REQUIRE(registrarWithParameters);
        CHECK(registrarWithParameters.value() == "sip:9991@lab.invalid:16060");

        const auto complete = polphone::sip::normalizeDestination(
            "sip:600@other.invalid:17060",
            "pbx.local",
            "sip:lab.invalid:16060");
        REQUIRE(complete);
        CHECK(complete.value() == "sip:600@other.invalid:17060");
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

    TEST_CASE("nomeia todos os estados de mídia tratados")
    {
        CHECK(polphone::sip::callMediaStatusName(PJSUA_CALL_MEDIA_NONE) == "NONE");
        CHECK(polphone::sip::callMediaStatusName(PJSUA_CALL_MEDIA_ACTIVE) == "ACTIVE");
        CHECK(polphone::sip::callMediaStatusName(PJSUA_CALL_MEDIA_LOCAL_HOLD)
              == "LOCAL_HOLD");
        CHECK(polphone::sip::callMediaStatusName(PJSUA_CALL_MEDIA_REMOTE_HOLD)
              == "REMOTE_HOLD");
        CHECK(polphone::sip::callMediaStatusName(PJSUA_CALL_MEDIA_ERROR) == "ERROR");
    }

    TEST_CASE("traduz respostas finais de chamada para ação do operador")
    {
        CHECK(polphone::sip::callStatusMessage(404, "Not Found")
              .find("não encontrado") != std::string::npos);
        CHECK(polphone::sip::callStatusMessage(408, "Request Timeout")
              .find("firewall") != std::string::npos);
        CHECK(polphone::sip::callStatusMessage(486, "Busy Here")
              .find("ocupado") != std::string::npos);
        CHECK(polphone::sip::callStatusMessage(503, "Service Unavailable")
              .find("indisponível") != std::string::npos);
        CHECK(polphone::sip::callStatusMessage(603, "Decline")
              .find("recusada") != std::string::npos);
        CHECK(polphone::sip::callStatusMessage(488, "Not Acceptable Here")
              .find("488 Not Acceptable Here") != std::string::npos);
    }
}
