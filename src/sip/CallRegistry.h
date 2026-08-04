/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include "util/Result.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace polphone::sip {

class SipCall;

class CallRegistry final {
public:
    class Lease final {
    public:
        Lease() = default;
        ~Lease();
        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        [[nodiscard]] SipCall* get() const noexcept;
        [[nodiscard]] SipCall* operator->() const noexcept;
        explicit operator bool() const noexcept;

    private:
        friend class CallRegistry;
        explicit Lease(SipCall* call) noexcept;
        void reset() noexcept;
        SipCall* call_{nullptr};
    };

    CallRegistry() = default;
    ~CallRegistry();

    CallRegistry(const CallRegistry&) = delete;
    CallRegistry& operator=(const CallRegistry&) = delete;

    [[nodiscard]] util::Result<void> adopt(std::unique_ptr<SipCall>& call);
    // Mantém a chamada viva enquanto operações externas usam o ponteiro.
    [[nodiscard]] Lease acquireCurrent() const noexcept;
    [[nodiscard]] bool hasActiveCall() const noexcept;
    void retire(SipCall* call) noexcept;
    void retireCurrent() noexcept;
    [[nodiscard]] std::size_t reap() noexcept;
    [[nodiscard]] std::size_t retiredCount() const noexcept;

    [[nodiscard]] bool waitUntilIdle(std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] bool waitUntilSafeToReap(std::chrono::milliseconds timeout) noexcept;
    void notifyCallbackComplete() noexcept;

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::unique_ptr<SipCall> current_;
    std::vector<std::unique_ptr<SipCall>> graveyard_;
};

} // namespace polphone::sip
