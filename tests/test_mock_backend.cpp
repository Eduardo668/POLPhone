/* POLPhone - testes determinísticos do modo de demonstração. GPL-2.0-only. */

#include "core/MockTelephonyBackend.h"
#include "core/PolPhoneController.h"

#include <doctest/doctest.h>

#include <chrono>
#include <string>

using namespace polphone::core;
using namespace std::chrono_literals;

namespace {

void readyAndRegistered(MockTelephonyBackend& backend)
{
    REQUIRE(backend.initialize());
    backend.tick(250ms);
    REQUIRE(backend.registerAccount());
    backend.tick(650ms);
    REQUIRE(backend.getState().registration == RegistrationState::Connected);
}

void confirmedCall(MockTelephonyBackend& backend)
{
    readyAndRegistered(backend);
    REQUIRE(backend.makeCall("5511999991234"));
    backend.tick(1050ms);
    REQUIRE(backend.getState().call == CallState::Active);
}

} // namespace

TEST_SUITE("mock-backend") {
    TEST_CASE("inicialização não acessa rede e termina desconectada")
    {
        MockTelephonyBackend backend;
        CHECK(backend.getState().demoMode);
        REQUIRE(backend.initialize());
        CHECK(backend.getState().application == ApplicationState::Initializing);
        backend.tick(249ms);
        CHECK(backend.getState().application == ApplicationState::Initializing);
        backend.tick(1ms);
        CHECK(backend.getState().application == ApplicationState::Ready);
        CHECK(backend.getState().registration == RegistrationState::Disconnected);
    }

    TEST_CASE("registro percorre conectando e conectado")
    {
        MockTelephonyBackend backend;
        REQUIRE(backend.initialize());
        backend.tick(250ms);
        REQUIRE(backend.registerAccount());
        CHECK(backend.getState().registration == RegistrationState::Connecting);
        backend.tick(650ms);
        CHECK(backend.getState().registration == RegistrationState::Connected);
    }

    TEST_CASE("chamada de saída percorre todos os estados e mascara destino")
    {
        MockTelephonyBackend backend;
        readyAndRegistered(backend);
        REQUIRE(backend.makeCall("5511999991234"));
        CHECK(backend.getState().call == CallState::OutgoingDialing);
        CHECK(backend.getState().callDirection == CallDirection::Outgoing);
        CHECK(backend.getState().maskedRemote == "*********1234");
        backend.tick(450ms);
        CHECK(backend.getState().call == CallState::OutgoingRinging);
        backend.tick(600ms);
        CHECK(backend.getState().call == CallState::Active);
        CHECK(backend.getState().media == MediaState::Active);
    }

    TEST_CASE("cronômetro avança apenas na chamada confirmada")
    {
        MockTelephonyBackend backend;
        confirmedCall(backend);
        CHECK(backend.getState().callDurationSeconds == 0);
        backend.tick(3100ms);
        CHECK(backend.getState().callDurationSeconds == 3);
        REQUIRE(backend.hangupCall());
        backend.tick(350ms);
        CHECK(backend.getState().callDurationSeconds == 0);
    }

    TEST_CASE("DTMF é recusado fora de chamada")
    {
        MockTelephonyBackend backend;
        readyAndRegistered(backend);
        CHECK_FALSE(backend.sendDtmf("5", DtmfMethod::Rfc4733));
    }

    TEST_CASE("DTMF usa exatamente um método e bloqueia concorrência")
    {
        MockTelephonyBackend backend;
        confirmedCall(backend);
        REQUIRE(backend.sendDtmf("12#", DtmfMethod::Inband));
        CHECK(backend.getState().dtmfInFlight);
        CHECK(backend.getState().lastDtmfMethod == DtmfMethod::Inband);
        CHECK_FALSE(backend.sendDtmf("5", DtmfMethod::SipInfo));
        backend.tick(780ms);
        CHECK_FALSE(backend.getState().dtmfInFlight);
    }

    TEST_CASE("configuração DTMF muda em runtime e o próximo envio usa o valor efetivo")
    {
        MockTelephonyBackend backend;
        confirmedCall(backend);
        CHECK(backend.getState().dtmfConfiguredMethod == DtmfMethod::Rfc4733);
        CHECK(backend.getState().dtmfEffectiveMethod == DtmfMethod::Rfc4733);

        REQUIRE(backend.applyDtmfSettings(
            {DtmfMethod::SipInfo, 240U, 140U, -8}));
        REQUIRE(backend.sendDtmf("5", DtmfMethod::Automatic));
        auto status = backend.getState();
        CHECK(status.dtmfConfiguredMethod == DtmfMethod::SipInfo);
        CHECK(status.dtmfEffectiveMethod == DtmfMethod::SipInfo);
        CHECK(status.lastDtmfMethod == DtmfMethod::SipInfo);
        CHECK(status.dtmfDurationMs == 240U);
        CHECK(status.dtmfGapMs == 140U);
        CHECK(status.inbandVolumeDbm0 == -8);
        backend.tick(500ms);
        CHECK(backend.getState().lastDtmfResult == "OK");

        REQUIRE(backend.applyDtmfSettings(
            {DtmfMethod::Inband, 300U, 90U, -4}));
        REQUIRE(backend.sendDtmf("6", DtmfMethod::Automatic));
        status = backend.getState();
        CHECK(status.dtmfConfiguredMethod == DtmfMethod::Inband);
        CHECK(status.dtmfEffectiveMethod == DtmfMethod::Inband);
        CHECK(status.lastDtmfMethod == DtmfMethod::Inband);
        CHECK(status.dtmfDurationMs == 300U);
        CHECK(status.dtmfGapMs == 90U);
        CHECK(status.inbandVolumeDbm0 == -4);
    }

    TEST_CASE("aplicar a mesma configuração DTMF é idempotente")
    {
        MockTelephonyBackend backend;
        const DtmfRuntimeSettings settings{DtmfMethod::SipInfo, 160U, 100U, -10};
        REQUIRE(backend.applyDtmfSettings(settings));
        const auto revision = backend.getState().revision;
        REQUIRE(backend.applyDtmfSettings(settings));
        CHECK(backend.getState().revision == revision);
    }

    TEST_CASE("mudo alterna somente durante chamada confirmada")
    {
        MockTelephonyBackend backend;
        readyAndRegistered(backend);
        CHECK_FALSE(backend.setMuted(true));
        REQUIRE(backend.makeCall("1002"));
        backend.tick(1050ms);
        REQUIRE(backend.setMuted(true));
        CHECK(backend.getState().muted);
        REQUIRE(backend.setMuted(false));
        CHECK_FALSE(backend.getState().muted);
    }

    TEST_CASE("chamada recebida pode ser atendida")
    {
        MockTelephonyBackend backend;
        readyAndRegistered(backend);
        REQUIRE(backend.simulate(DemoScenario::IncomingCall));
        CHECK(backend.getState().call == CallState::IncomingRinging);
        CHECK(backend.getState().callDirection == CallDirection::Incoming);
        CHECK(backend.getState().remoteDisplayName == "Maria Demo");
        REQUIRE(backend.answerCall());
        CHECK(backend.getState().call == CallState::Connecting);
        backend.tick(450ms);
        CHECK(backend.getState().call == CallState::Active);
    }

    TEST_CASE("chamada recebida pode ser rejeitada")
    {
        MockTelephonyBackend backend;
        readyAndRegistered(backend);
        REQUIRE(backend.simulate(DemoScenario::IncomingCall));
        REQUIRE(backend.rejectCall());
        CHECK(backend.getState().call == CallState::Disconnecting);
        backend.tick(300ms);
        CHECK(backend.getState().call == CallState::Idle);
    }

    TEST_CASE("simula erro de registro com detalhe técnico seguro")
    {
        MockTelephonyBackend backend;
        REQUIRE(backend.initialize());
        backend.tick(250ms);
        REQUIRE(backend.simulate(DemoScenario::RegistrationFailure));
        backend.tick(500ms);
        CHECK(backend.getState().registration == RegistrationState::Failed);
        CHECK_FALSE(backend.getState().friendlyError.empty());
        CHECK(backend.getState().technicalDetail == "DEMO_REGISTRATION_TIMEOUT");
    }

    TEST_CASE("simula erro de chamada")
    {
        MockTelephonyBackend backend;
        readyAndRegistered(backend);
        REQUIRE(backend.simulate(DemoScenario::CallFailure));
        backend.tick(600ms);
        CHECK(backend.getState().call == CallState::Error);
        CHECK(backend.getState().technicalDetail.find("486") != std::string::npos);
    }

    TEST_CASE("simula perda de conexão encerrando chamada")
    {
        MockTelephonyBackend backend;
        confirmedCall(backend);
        REQUIRE(backend.simulate(DemoScenario::ConnectionLoss));
        CHECK(backend.getState().registration == RegistrationState::Failed);
        CHECK(backend.getState().call == CallState::Idle);
        CHECK(backend.getState().media == MediaState::Inactive);
    }

    TEST_CASE("diagnóstico do mock não registra dígitos DTMF nem segredos")
    {
        MockTelephonyBackend backend;
        confirmedCall(backend);
        REQUIRE(backend.sendDtmf("9876", DtmfMethod::SipInfo));
        for (const auto& line : backend.getState().sanitizedLogs) {
            CHECK(line.find("9876") == std::string::npos);
            CHECK(line.find("password") == std::string::npos);
            CHECK(line.find("Authorization") == std::string::npos);
        }
    }

    TEST_CASE("troca dispositivos simulados com validação de direção")
    {
        MockTelephonyBackend backend;
        REQUIRE(backend.selectAudioDevice(AudioDirection::Capture, 0));
        CHECK(backend.getState().captureDevice == 0);
        CHECK_FALSE(backend.selectAudioDevice(AudioDirection::Playback, 0));
        REQUIRE(backend.selectAudioDevice(AudioDirection::Playback, 2));
        CHECK(backend.getState().playbackDevice == 2);
    }

    TEST_CASE("fachada lista dispositivos simulados fora da thread chamadora")
    {
        PolPhoneController controller(std::make_unique<MockTelephonyBackend>());
        auto listed = controller.listAudioDevices().get();
        REQUIRE(listed);
        CHECK(listed.value().size() == 3);
        CHECK(listed.value().at(1).capture);
        CHECK(listed.value().at(1).playback);
    }

    TEST_CASE("shutdown é ordenado e idempotente")
    {
        MockTelephonyBackend backend;
        confirmedCall(backend);
        REQUIRE(backend.shutdown());
        CHECK(backend.getState().application == ApplicationState::ShuttingDown);
        backend.tick(200ms);
        CHECK(backend.getState().application == ApplicationState::Stopped);
        CHECK(backend.getState().call == CallState::Idle);
        CHECK(backend.shutdown());
    }
}
