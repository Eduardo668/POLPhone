/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "dtmf/DtmfMethod.h"
#include "dtmf/DtmfPlan.h"
#include "dtmf/DtmfRequestGate.h"
#include "dtmf/DtmfSender.h"
#include "audio/ToneGenerator.h"
#include "app/AppState.h"
#include "app/EventQueue.h"
#include "logging/Logger.h"
#include "sip/SipCall.h"
#include "sip/CallRegistry.h"

#include <doctest/doctest.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

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

    TEST_CASE("cada método resolve para exatamente um caminho de execução")
    {
        using polphone::dtmf::DtmfExecutionPath;
        using polphone::dtmf::DtmfMethod;
        CHECK(polphone::dtmf::executionPathFor(DtmfMethod::Rfc4733)
              == DtmfExecutionPath::Rfc4733);
        CHECK(polphone::dtmf::executionPathFor(DtmfMethod::Inband)
              == DtmfExecutionPath::Inband);
        CHECK(polphone::dtmf::executionPathFor(DtmfMethod::SipInfo)
              == DtmfExecutionPath::SipInfo);
        CHECK_FALSE(polphone::dtmf::executionPathFor(
            static_cast<DtmfMethod>(999)));
    }

    TEST_CASE("gate recusa requisição concorrente e informa o id em execução")
    {
        polphone::dtmf::DtmfRequestGate gate;
        std::mutex synchronizationMutex;
        std::condition_variable synchronization;
        bool ready = false;
        bool release = false;
        std::atomic<bool> firstSucceeded{false};

        std::thread first([&] {
            const auto started = gate.begin("dtmf-concorrente-1");
            firstSucceeded = static_cast<bool>(started);
            {
                std::lock_guard<std::mutex> lock(synchronizationMutex);
                ready = true;
            }
            synchronization.notify_one();
            std::unique_lock<std::mutex> lock(synchronizationMutex);
            synchronization.wait(lock, [&] { return release; });
            if (started) gate.finish();
        });

        {
            std::unique_lock<std::mutex> lock(synchronizationMutex);
            synchronization.wait(lock, [&] { return ready; });
        }
        CHECK(firstSucceeded.load());
        const auto refused = gate.begin("dtmf-concorrente-2");
        CHECK_FALSE(refused);
        if (!refused) {
            CHECK(refused.error().detail == "dtmf-concorrente-1");
        }
        CHECK(gate.inFlight());
        CHECK(gate.currentCorrelationId() == "dtmf-concorrente-1");

        {
            std::lock_guard<std::mutex> lock(synchronizationMutex);
            release = true;
        }
        synchronization.notify_one();
        first.join();
        CHECK_FALSE(gate.inFlight());
        gate.finish();
        CHECK(gate.begin("dtmf-concorrente-3"));
        gate.finish();
    }

    TEST_CASE("defaults DTMF mudam em runtime com validação de faixa")
    {
        polphone::sip::CallRegistry calls;
        polphone::app::AppState state;
        polphone::app::EventQueue events;
        polphone::logging::Logger logger;
        polphone::dtmf::DtmfSender sender(calls, state, events, logger);
        polphone::config::DtmfConfig config;
        REQUIRE(sender.configure(config));

        CHECK(sender.setDefaultMethod(polphone::dtmf::DtmfMethod::Inband));
        CHECK(sender.setDurationMs(250));
        CHECK(sender.setGapMs(150));
        CHECK(sender.setVolumeDbm0(-5));
        const auto changed = sender.settings();
        CHECK(changed.defaultMethod == polphone::dtmf::DtmfMethod::Inband);
        CHECK(changed.durationMs == 250U);
        CHECK(changed.gapMs == 150U);
        CHECK(changed.volumeDbm0 == -5);

        CHECK_FALSE(sender.setDurationMs(39));
        CHECK_FALSE(sender.setDurationMs(2001));
        CHECK_FALSE(sender.setGapMs(19));
        CHECK_FALSE(sender.setGapMs(2001));
        CHECK_FALSE(sender.setVolumeDbm0(-31));
        CHECK_FALSE(sender.setVolumeDbm0(1));
        CHECK(sender.settings().durationMs == 250U);

        const auto requestOverride = sender.send(polphone::dtmf::DtmfRequest{
            "5", polphone::dtmf::DtmfMethod::SipInfo, 400U, 300U, -20});
        CHECK_FALSE(requestOverride);
        const auto unchanged = sender.settings();
        CHECK(unchanged.defaultMethod == polphone::dtmf::DtmfMethod::Inband);
        CHECK(unchanged.durationMs == 250U);
        CHECK(unchanged.gapMs == 150U);
        CHECK(unchanged.volumeDbm0 == -5);
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

    TEST_CASE("sender aplica os mesmos guards ao método in-band")
    {
        polphone::sip::CallRegistry calls;
        polphone::app::AppState state;
        polphone::app::EventQueue events;
        polphone::logging::Logger logger;
        polphone::dtmf::DtmfSender sender(calls, state, events, logger);

        const auto result = sender.send(polphone::dtmf::DtmfRequest{
            "5", polphone::dtmf::DtmfMethod::Inband, 160U, 100U, -10});
        CHECK_FALSE(result);
        CHECK(result.error().message.find("Sem chamada ativa") != std::string::npos);
        CHECK_FALSE(sender.inFlight());
    }

    TEST_CASE("converte dBm0 para amplitude PCM limitada")
    {
        CHECK(polphone::audio::ToneGenerator::MaxQueuedDigits == 32U);
        CHECK(polphone::audio::toneAmplitudeFromDbm0(0) == 32767);
        CHECK(polphone::audio::toneAmplitudeFromDbm0(-10) == 10362);
        CHECK(polphone::audio::toneAmplitudeFromDbm0(-20) == 3277);
        CHECK(polphone::audio::toneAmplitudeFromDbm0(-30) == 1036);
        CHECK(polphone::audio::toneAmplitudeFromDbm0(-99) == 1036);
        CHECK(polphone::audio::toneAmplitudeFromDbm0(10) == 32767);
    }

    TEST_CASE("valida o formato de áudio e o volume antes do in-band")
    {
        polphone::sip::CallRegistry calls;
        polphone::app::AppState state;
        polphone::app::EventQueue events;
        polphone::logging::Logger logger;
        polphone::dtmf::DtmfSender sender(calls, state, events, logger);
        polphone::config::DtmfConfig dtmf;
        polphone::config::AudioConfig audio;
        audio.clockRate = 0;
        CHECK_FALSE(sender.configure(dtmf, audio));

        const auto invalidVolume = sender.send(polphone::dtmf::DtmfRequest{
            "5", polphone::dtmf::DtmfMethod::Inband, 160U, 100U, -31});
        CHECK_FALSE(invalidVolume);
        CHECK(invalidVolume.error().message.find("volume DTMF")
              != std::string::npos);
    }

    TEST_CASE("traduz respostas finais do SIP INFO")
    {
        CHECK(polphone::dtmf::evaluateSipInfoResponse(200, "OK", false));
        CHECK(polphone::dtmf::evaluateSipInfoResponse(202, "Accepted", false));

        const auto media = polphone::dtmf::evaluateSipInfoResponse(
            415, "Unsupported Media Type", false);
        REQUIRE_FALSE(media);
        CHECK(media.error().message.find("application/dtmf-relay")
              != std::string::npos);

        const auto dialog = polphone::dtmf::evaluateSipInfoResponse(
            481, "Call/Transaction Does Not Exist", false);
        REQUIRE_FALSE(dialog);
        CHECK(dialog.error().message.find("diálogo SIP") != std::string::npos);

        const auto unsupported = polphone::dtmf::evaluateSipInfoResponse(
            501, "Not Implemented", false);
        REQUIRE_FALSE(unsupported);
        CHECK(unsupported.error().message.find("não implementa")
              != std::string::npos);
    }

    TEST_CASE("traduz timeout e rejeição genérica do SIP INFO")
    {
        const auto timeout = polphone::dtmf::evaluateSipInfoResponse(
            408, "Request Timeout", true);
        REQUIRE_FALSE(timeout);
        CHECK(timeout.error().message.find("expirou") != std::string::npos);

        const auto rejected = polphone::dtmf::evaluateSipInfoResponse(
            488, "Not Acceptable Here", false);
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error().detail == "488 Not Acceptable Here");
    }
}
