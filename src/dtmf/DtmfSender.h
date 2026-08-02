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
#include "dtmf/DtmfRequestGate.h"
#include "logging/Logger.h"
#include "util/Result.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace polphone::audio {
class ToneGenerator;
}

namespace polphone::sip {
class CallRegistry;
class SipCall;
} // namespace polphone::sip

namespace polphone::dtmf {

[[nodiscard]] util::Result<void> evaluateSipInfoResponse(
    int statusCode,
    std::string_view statusText,
    bool timedOut);

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
    ~DtmfSender();

    DtmfSender(const DtmfSender&) = delete;
    DtmfSender& operator=(const DtmfSender&) = delete;

    [[nodiscard]] util::Result<void> configure(
        const config::DtmfConfig& config,
        const config::AudioConfig& audio = config::AudioConfig{});
    [[nodiscard]] DtmfSettings settings() const;
    [[nodiscard]] util::Result<void> setDefaultMethod(DtmfMethod method);
    [[nodiscard]] util::Result<void> setDurationMs(int durationMs);
    [[nodiscard]] util::Result<void> setGapMs(int gapMs);
    [[nodiscard]] util::Result<void> setVolumeDbm0(int volumeDbm0);
    [[nodiscard]] util::Result<DtmfResult> send(const DtmfRequest& request);
    [[nodiscard]] bool inFlight() const noexcept;

private:
    class InFlightGuard final {
    public:
        explicit InFlightGuard(DtmfSender& sender) noexcept;
        ~InFlightGuard();
        void dismiss() noexcept;

        InFlightGuard(const InFlightGuard&) = delete;
        InFlightGuard& operator=(const InFlightGuard&) = delete;

    private:
        DtmfSender& sender_;
        bool active_{true};
    };

    [[nodiscard]] util::Result<sip::SipCall*> activeCall() const;
    [[nodiscard]] util::Result<int> telephoneEventPayloadType(sip::SipCall& call) const;
    [[nodiscard]] util::Result<void> sendRfc4733(
        sip::SipCall& call,
        char digit,
        unsigned durationMs);
    [[nodiscard]] util::Result<void> sendSipInfo(
        sip::SipCall& call,
        char digit,
        unsigned durationMs,
        std::string_view correlationId);
    [[nodiscard]] std::string nextCorrelationId();
    [[nodiscard]] util::Result<DtmfResult> startInband(
        sip::SipCall& call,
        std::vector<DtmfPlanStep> plan,
        const DtmfRequest& request,
        std::string correlationId);
    void runInband(
        sip::SipCall* call,
        std::vector<DtmfPlanStep> plan,
        DtmfRequest request,
        std::string correlationId) noexcept;
    [[nodiscard]] util::Result<void> performInband(
        sip::SipCall& call,
        const std::vector<DtmfPlanStep>& plan,
        const DtmfRequest& request,
        std::string_view correlationId);
    [[nodiscard]] bool waitCancelable(unsigned milliseconds) const noexcept;
    void reapWorker() noexcept;
    void publishInbandFailure(
        std::string_view correlationId,
        const util::Error& error) noexcept;

    sip::CallRegistry& calls_;
    app::AppState& state_;
    app::EventQueue& events_;
    logging::Logger& logger_;
    mutable std::mutex settingsMutex_;
    DtmfSettings settings_;
    DtmfRequestGate requestGate_;
    std::atomic<unsigned long long> sequence_{0U};
    unsigned audioClockRate_{8000U};
    unsigned audioChannelCount_{1U};
    unsigned audioPtimeMs_{20U};
    std::unique_ptr<audio::ToneGenerator> toneGenerator_;
    mutable std::mutex workerMutex_;
    std::thread worker_;
    std::atomic<bool> cancelWorker_{false};
};

} // namespace polphone::dtmf
