/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "sip/PjErrors.h"
#include "app/ExitCodes.h"
#include "logging/PjLogWriter.h"
#include "sip/SipAccount.h"
#include "sip/SipCall.h"

#include <pjmedia-audiodev/errno.h>
#include <pjmedia/errno.h>
#include <pjsip/sip_errno.h>

#include <doctest/doctest.h>

#include <string>
#include <utility>

static_assert(noexcept(std::declval<polphone::logging::PjLogWriter&>().write(
    std::declval<const pj::LogEntry&>())));
static_assert(noexcept(std::declval<polphone::sip::SipAccount&>().onRegState(
    std::declval<pj::OnRegStateParam&>())));
static_assert(noexcept(std::declval<polphone::sip::SipAccount&>().onIncomingCall(
    std::declval<pj::OnIncomingCallParam&>())));
static_assert(noexcept(std::declval<polphone::sip::SipCall&>().onCallState(
    std::declval<pj::OnCallStateParam&>())));
static_assert(noexcept(std::declval<polphone::sip::SipCall&>().onCallTsxState(
    std::declval<pj::OnCallTsxStateParam&>())));
static_assert(noexcept(std::declval<polphone::sip::SipCall&>().onCallMediaState(
    std::declval<pj::OnCallMediaStateParam&>())));

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

    TEST_CASE("traduz erros PJSIP obrigatórios para ação do operador")
    {
        CHECK(polphone::sip::friendlyPjMessage(
                  PJSIP_EBUSY, "Endpoint::transportCreate")
              .find("network.localPort: 0") != std::string::npos);
#ifdef _WIN32
        CHECK(polphone::sip::friendlyPjMessage(
                  PJ_STATUS_FROM_OS(10048), "transportCreate")
              .find("Porta UDP local") != std::string::npos);
#endif
        CHECK(polphone::sip::friendlyPjMessage(
                  PJMEDIA_EAUD_NODEFDEV, "audDevManager")
              .find("devices") != std::string::npos);
        CHECK(polphone::sip::friendlyPjMessage(
                  PJMEDIA_RTP_EREMNORFC2833, "sendDtmf")
              .find("telephone-event") != std::string::npos);
        CHECK(polphone::sip::friendlyPjMessage(PJ_ETIMEDOUT, "REGISTER")
              .find("firewall") != std::string::npos);
    }

    TEST_CASE("códigos de saída distinguem configuração e inicialização")
    {
        using polphone::app::initializationExitCode;
        using polphone::util::ErrorCode;
        CHECK(initializationExitCode(ErrorCode::NotFound) == 1);
        CHECK(initializationExitCode(ErrorCode::Io) == 1);
        CHECK(initializationExitCode(ErrorCode::Parse) == 1);
        CHECK(initializationExitCode(ErrorCode::Validation) == 1);
        CHECK(initializationExitCode(ErrorCode::InvalidArgument) == 1);
        CHECK(initializationExitCode(ErrorCode::Pjsip) == 2);
        CHECK(initializationExitCode(ErrorCode::Runtime) == 2);
        CHECK(polphone::app::ExitRuntime == 3);
        CHECK(polphone::app::ExitInterrupted == 130);
    }
}
