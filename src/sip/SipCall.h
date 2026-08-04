/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

// O PJSIP deve vir antes dos headers do Windows para evitar conflito de Winsock.
#include <pjsua2.hpp>

#include "app/AppState.h"
#include "app/EventQueue.h"
#include "logging/Logger.h"
#include "util/Result.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace polphone::sip {

class CallRegistry;

struct DtmfInfoResponse {
    int statusCode{0};
    std::string statusText;
    bool timedOut{false};
};

struct CallDiagnostics {
    std::string codec;
    std::string localRtp;
    std::string remoteRtp;
    std::uint64_t packetsSent{0};
    std::uint64_t packetsReceived{0};
    std::uint64_t packetsLost{0};
    double jitterMs{0.0};
    bool hasRtpStatistics{false};
};

[[nodiscard]] util::Result<std::string> normalizeDestination(
    std::string_view destination,
    std::string_view domain,
    std::string_view registrarUri = {});
[[nodiscard]] app::CallState callStateFromPjsip(pjsip_inv_state state) noexcept;
[[nodiscard]] std::string_view callMediaStatusName(
    pjsua_call_media_status status) noexcept;
[[nodiscard]] std::string callStatusMessage(int sipCode,
                                            std::string_view reason);

class SipCall final : public pj::Call {
public:
    SipCall(pj::Account& account,
            CallRegistry& registry,
            app::AppState& state,
            app::EventQueue& events,
            logging::Logger& logger,
            app::CallDirection direction,
            int callId = PJSUA_INVALID_ID);
    ~SipCall() override;

    SipCall(const SipCall&) = delete;
    SipCall& operator=(const SipCall&) = delete;

    [[nodiscard]] util::Result<void> start(std::string destinationUri);
    [[nodiscard]] util::Result<void> announceIncoming();
    [[nodiscard]] util::Result<void> answer(int statusCode = PJSIP_SC_OK);
    [[nodiscard]] util::Result<void> hangupCall();
    [[nodiscard]] bool canReap() const noexcept;
    void retainExternalUse() noexcept;
    void releaseExternalUse() noexcept;
    [[nodiscard]] static std::size_t liveCount() noexcept;
    [[nodiscard]] std::optional<pj::AudioMedia> audioMedia() const;
    [[nodiscard]] int audioConfSlot() const noexcept;
    [[nodiscard]] bool hasActiveAudio() const noexcept;
    [[nodiscard]] util::Result<void> setMuted(bool muted);
    [[nodiscard]] bool isMuted() const noexcept;
    [[nodiscard]] app::CallDirection direction() const noexcept;
    [[nodiscard]] CallDiagnostics diagnostics() const;
    [[nodiscard]] util::Result<DtmfInfoResponse> sendDtmfInfo(
        char digit,
        unsigned durationMs,
        std::string correlationId,
        std::chrono::milliseconds timeout = std::chrono::seconds(5));

    void onCallState(pj::OnCallStateParam& parameter) noexcept override;
    void onCallTsxState(pj::OnCallTsxStateParam& parameter) noexcept override;
    void onCallMediaState(pj::OnCallMediaStateParam& parameter) noexcept override;

private:
    class CallbackScope final {
    public:
        explicit CallbackScope(SipCall& call) noexcept;
        ~CallbackScope();

    private:
        SipCall& call_;
    };

    void publishCallbackFailure(std::string_view callback,
                                std::string_view detail) noexcept;
    void publishPjCallbackFailure(std::string_view callback,
                                  const pj::Error& error) noexcept;
    [[nodiscard]] util::Result<void> connectAudio(unsigned mediaIndex);
    void disconnectAudio(bool retainMedia = false,
                         bool stopTransmissions = true) noexcept;
    void publishMediaFailure(std::string_view message,
                             std::string_view detail = {}) noexcept;
    void handleInfoTransaction(const pj::SipTransaction& transaction) noexcept;
    void failPendingInfo(int statusCode, std::string_view statusText) noexcept;

    struct PendingInfo final {
        std::string correlationId;
        void* transaction{nullptr};
        int statusCode{0};
        std::string statusText;
        bool completed{false};
        bool timedOut{false};
    };

    CallRegistry& registry_;
    app::AppState& state_;
    app::EventQueue& events_;
    logging::Logger& logger_;
    const app::CallDirection direction_;
    std::string destinationUri_;
    mutable std::mutex mediaMutex_;
    std::optional<pj::AudioMedia> audioMedia_;
    std::atomic<int> audioConfSlot_{PJSUA_INVALID_ID};
    std::atomic<bool> audioConnected_{false};
    std::atomic<bool> muted_{false};
    // Somente a primeira decisão local final (200/486/etc.) pode produzir
    // sinalização. Isso torna duplo clique e comandos atrasados inofensivos.
    std::atomic<int> incomingFinalResponse_{0};
    std::atomic<bool> hangupRequested_{false};
    bool playbackConnected_{false};
    bool captureConnected_{false};
    unsigned activeMediaIndex_{0U};
    CallDiagnostics diagnostics_;
    mutable std::mutex infoMutex_;
    std::condition_variable infoCondition_;
    std::shared_ptr<PendingInfo> pendingInfo_;
    std::deque<std::shared_ptr<PendingInfo>> unboundInfo_;
    std::unordered_map<void*, std::shared_ptr<PendingInfo>> infoTransactions_;
    std::atomic<unsigned> callbackDepth_{0U};
    std::atomic<unsigned> externalUseDepth_{0U};
    static std::atomic<std::size_t> liveCount_;
};

} // namespace polphone::sip
