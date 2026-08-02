/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "app/AppState.h"
#include "app/EventQueue.h"
#include "sip/SipAccount.h"

#include <doctest/doctest.h>

#include <string>

TEST_SUITE("registration") {
    TEST_CASE("AppState publica snapshot por cópia")
    {
        polphone::app::AppState state;
        state.updateRegistration({
            polphone::app::RegistrationState::Registered,
            true,
            200,
            300,
            "OK",
            "Registro SIP aceito."});
        auto snapshot = state.registration();
        CHECK(snapshot.state == polphone::app::RegistrationState::Registered);
        CHECK(snapshot.active);
        CHECK(snapshot.sipCode == 200);
        CHECK(snapshot.expiresSec == 300);
        snapshot.message.clear();
        CHECK_FALSE(state.registration().message.empty());
    }

    TEST_CASE("EventQueue preserva ordem e drena sem duplicar")
    {
        polphone::app::EventQueue queue;
        CHECK(queue.push({polphone::app::UiEventSeverity::Info, "sip", "primeiro"}));
        CHECK(queue.push({polphone::app::UiEventSeverity::Error, "sip", "segundo"}));
        CHECK(queue.size() == 2);
        const auto events = queue.drain();
        REQUIRE(events.size() == 2);
        CHECK(events[0].text == "primeiro");
        CHECK(events[1].text == "segundo");
        CHECK(queue.size() == 0);
    }

    TEST_CASE("AppState publica estado de chamada por cópia")
    {
        polphone::app::AppState state;
        state.updateCall({
            polphone::app::CallState::Confirmed,
            200,
            "OK",
            "sip:1002@pbx.local"});
        auto snapshot = state.call();
        CHECK(snapshot.state == polphone::app::CallState::Confirmed);
        CHECK(snapshot.sipCode == 200);
        CHECK(snapshot.remoteUri == "sip:1002@pbx.local");
        snapshot.remoteUri.clear();
        CHECK_FALSE(state.call().remoteUri.empty());
    }

    TEST_CASE("traduz respostas de registro obrigatórias")
    {
        CHECK(polphone::sip::registrationStatusMessage(401, PJ_SUCCESS, "Unauthorized")
              .find("usuário") != std::string::npos);
        CHECK(polphone::sip::registrationStatusMessage(403, PJ_SUCCESS, "Forbidden")
              .find("proibido") != std::string::npos);
        CHECK(polphone::sip::registrationStatusMessage(404, PJ_SUCCESS, "Not Found")
              .find("não encontrado") != std::string::npos);
        CHECK(polphone::sip::registrationStatusMessage(408, PJ_SUCCESS, "Timeout")
              .find("firewall") != std::string::npos);
        CHECK(polphone::sip::registrationStatusMessage(503, PJ_SUCCESS, "Unavailable")
              .find("indisponível") != std::string::npos);
        CHECK(polphone::sip::registrationStatusMessage(0, PJ_ETIMEDOUT, "")
              .find("Sem resposta") != std::string::npos);
    }
}
