/* POLPhone - testes das regras de apresentação. GPL-2.0-only. */

#include "core/Presentation.h"

#include <ostream>
#include <doctest/doctest.h>

using namespace polphone::core;

namespace {

TelephonySnapshot connectedIdle()
{
    TelephonySnapshot state;
    state.application = ApplicationState::Ready;
    state.registration = RegistrationState::Connected;
    state.call = CallState::Idle;
    return state;
}

} // namespace

TEST_SUITE("core-presentation") {
    TEST_CASE("traduz todos os estados para texto visível")
    {
        CHECK(applicationStateText(ApplicationState::Initializing) == "Inicializando");
        CHECK(registrationStateText(RegistrationState::Disconnected) == "Desconectado");
        CHECK(registrationStateText(RegistrationState::Connecting) == "Conectando");
        CHECK(registrationStateText(RegistrationState::Connected) == "Conectado");
        CHECK(callStateText(CallState::Calling) == "Chamando");
        CHECK(callStateText(CallState::Confirmed) == "Em chamada");
        CHECK(callStateText(CallState::Ending) == "Encerrando");
        CHECK(mediaStateText(MediaState::Active) == "Áudio ativo");
    }

    TEST_CASE("valida números e URIs sem aceitar heurísticas ambíguas")
    {
        CHECK(validateDestination(" 1002 ").value() == "1002");
        CHECK(validateDestination("*43").value() == "*43");
        CHECK(validateDestination("+5511999999999"));
        CHECK(validateDestination("sip:1002@pbx.invalid"));
        CHECK_FALSE(validateDestination(""));
        CHECK_FALSE(validateDestination("alice"));
        CHECK_FALSE(validateDestination("sip:1002"));
        CHECK_FALSE(validateDestination("1002 1003"));
    }

    TEST_CASE("mascara destinos antes de apresentá-los")
    {
        CHECK(maskDestination("sip:5511999991234@pbx.invalid")
              == "sip:*********1234@pbx.invalid");
    }

    TEST_CASE("formata cronômetro curto e longo")
    {
        CHECK(formatCallDuration(0) == "00:00");
        CHECK(formatCallDuration(65) == "01:05");
        CHECK(formatCallDuration(3661) == "01:01:01");
    }

    TEST_CASE("comandos seguem registro e estado da chamada")
    {
        auto state = connectedIdle();
        auto commands = commandAvailability(state);
        CHECK(commands.makeCall);
        CHECK(commands.unregisterAccount);
        CHECK_FALSE(commands.answerCall);
        CHECK_FALSE(commands.sendDtmf);

        state.call = CallState::Incoming;
        commands = commandAvailability(state);
        CHECK(commands.answerCall);
        CHECK(commands.rejectCall);
        CHECK(commands.hangupCall);
        CHECK_FALSE(commands.makeCall);

        state.call = CallState::Confirmed;
        state.media = MediaState::Active;
        commands = commandAvailability(state);
        CHECK(commands.mute);
        CHECK(commands.keypad);
        CHECK(commands.sendDtmf);
    }

    TEST_CASE("DTMF permanece indisponível sem mídia ou durante envio")
    {
        auto state = connectedIdle();
        state.call = CallState::Confirmed;
        CHECK_FALSE(commandAvailability(state).sendDtmf);
        state.media = MediaState::Active;
        state.dtmfInFlight = true;
        CHECK_FALSE(commandAvailability(state).sendDtmf);
    }

    TEST_CASE("view model valida chamada e impede clique duplicado")
    {
        PhoneViewModel model;
        model.update(connectedIdle());
        CHECK_FALSE(model.beginCall("alice"));
        CHECK_FALSE(model.validationMessage().empty());
        CHECK(model.beginCall("1002"));
        CHECK(model.destination() == "1002");
        CHECK_FALSE(model.beginCall("1003"));
        model.completeCallCommand();
        CHECK(model.beginCall("1003"));
    }

    TEST_CASE("view model impede hangup e DTMF duplicados")
    {
        PhoneViewModel model;
        auto state = connectedIdle();
        state.call = CallState::Confirmed;
        state.media = MediaState::Active;
        model.update(state);
        CHECK(model.beginHangup());
        CHECK_FALSE(model.beginHangup());
        model.completeHangupCommand();
        CHECK(model.beginHangup());
        model.completeHangupCommand();
        CHECK(model.beginDtmf());
        CHECK_FALSE(model.beginDtmf());
        model.completeDtmf();
        CHECK(model.beginDtmf());
    }

    TEST_CASE("Modo URA seleciona somente In-band e restaura seleção anterior")
    {
        PhoneViewModel model;
        model.setDtmfMethod(DtmfMethod::SipInfo);
        model.setIvrMode(true);
        CHECK(model.ivrMode());
        CHECK(model.ivrStatusText() == "Modo URA ativo");
        CHECK(model.selectedDtmfMethod() == DtmfMethod::Inband);
        model.setIvrMode(false);
        CHECK_FALSE(model.ivrMode());
        CHECK(model.selectedDtmfMethod() == DtmfMethod::SipInfo);
    }

    TEST_CASE("escolher outro método desativa Modo URA explicitamente")
    {
        PhoneViewModel model;
        model.setIvrMode(true);
        model.setDtmfMethod(DtmfMethod::Rfc4733);
        CHECK_FALSE(model.ivrMode());
        CHECK(model.selectedDtmfMethod() == DtmfMethod::Rfc4733);
    }
}
