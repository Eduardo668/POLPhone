/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include "app/AppState.h"
#include "app/EventQueue.h"
#include "config/AppConfig.h"
#include "dtmf/DtmfMethod.h"
#include "dtmf/DtmfPlan.h"
#include "logging/Logger.h"
#include "util/Result.h"

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace polphone::sip {
class CallRegistry;
class SipCall;
} // namespace polphone::sip

namespace polphone::dtmf {

struct DtmfSettings {
    DtmfMethod defaultMethod{DtmfMethod::Rfc4733};
    unsigned durationMs{160U};
    unsigned gapMs{100U};
    int volumeDbm0{-10};
    bool localFeedback{false};
    bool logDigits{false};
};

struct DtmfRequest {
    std::string digits;
    DtmfMethod method{DtmfMethod::Rfc4733};
    unsigned durationMs{160U};
    unsigned gapMs{100U};
    int volumeDbm0{-10};
};

struct DtmfDigitResult {
    char digit{'\0'};
    bool ok{false};
    std::string detail;
};

struct DtmfResult {
    bool ok{false};
    std::string correlationId;
    std::vector<DtmfDigitResult> perDigit;
    std::string summary;
};

class DtmfSender final {
public:
    DtmfSender(sip::CallRegistry& calls,
               app::AppState& state,
               app::EventQueue& events,
               logging::Logger& logger) noexcept;

    DtmfSender(const DtmfSender&) = delete;
    DtmfSender& operator=(const DtmfSender&) = delete;

    [[nodiscard]] util::Result<void> configure(const config::DtmfConfig& config);
    [[nodiscard]] DtmfSettings settings() const;
    [[nodiscard]] util::Result<DtmfResult> send(const DtmfRequest& request);
    [[nodiscard]] bool inFlight() const noexcept;

private:
    class InFlightGuard final {
    public:
        explicit InFlightGuard(DtmfSender& sender) noexcept;
        ~InFlightGuard();

        InFlightGuard(const InFlightGuard&) = delete;
        InFlightGuard& operator=(const InFlightGuard&) = delete;

    private:
        DtmfSender& sender_;
    };

    [[nodiscard]] util::Result<void> begin(std::string correlationId);
    void finish() noexcept;
    [[nodiscard]] util::Result<sip::SipCall*> activeCall() const;
    [[nodiscard]] util::Result<int> telephoneEventPayloadType(sip::SipCall& call) const;
    [[nodiscard]] util::Result<void> sendRfc4733(
        sip::SipCall& call,
        char digit,
        unsigned durationMs);
    [[nodiscard]] std::string nextCorrelationId();

    sip::CallRegistry& calls_;
    app::AppState& state_;
    app::EventQueue& events_;
    logging::Logger& logger_;
    mutable std::mutex mutex_;
    DtmfSettings settings_;
    bool inFlight_{false};
    std::string currentCorrelationId_;
    std::atomic<unsigned long long> sequence_{0U};
};

} // namespace polphone::dtmf
