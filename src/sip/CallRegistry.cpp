/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "sip/CallRegistry.h"

#include "sip/SipCall.h"

#include <algorithm>
#include <utility>

namespace polphone::sip {

CallRegistry::Lease::Lease(SipCall* call) noexcept : call_(call) {}

CallRegistry::Lease::~Lease() { reset(); }

CallRegistry::Lease::Lease(Lease&& other) noexcept
    : call_(std::exchange(other.call_, nullptr))
{
}

CallRegistry::Lease& CallRegistry::Lease::operator=(Lease&& other) noexcept
{
    if (this != &other) {
        reset();
        call_ = std::exchange(other.call_, nullptr);
    }
    return *this;
}

SipCall* CallRegistry::Lease::get() const noexcept { return call_; }
SipCall* CallRegistry::Lease::operator->() const noexcept { return call_; }
CallRegistry::Lease::operator bool() const noexcept { return call_ != nullptr; }

void CallRegistry::Lease::reset() noexcept
{
    if (call_ != nullptr) {
        call_->releaseExternalUse();
        call_ = nullptr;
    }
}

CallRegistry::~CallRegistry() = default;

util::Result<void> CallRegistry::adopt(std::unique_ptr<SipCall>& call)
{
    if (call == nullptr) {
        return util::Result<void>::failure(
            util::ErrorCode::InvalidArgument,
            "A chamada a adotar é inválida; tente novamente.");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_ != nullptr) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Já existe uma chamada ativa; encerre-a antes de iniciar outra.");
    }
    current_ = std::move(call);
    return util::Result<void>::success();
}

CallRegistry::Lease CallRegistry::acquireCurrent() const noexcept
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (current_ == nullptr) return Lease{};
        current_->retainExternalUse();
        return Lease{current_.get()};
    } catch (...) {
        return Lease{};
    }
}

bool CallRegistry::hasActiveCall() const noexcept
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_ != nullptr;
    } catch (...) {
        return true;
    }
}

void CallRegistry::retire(SipCall* call) noexcept
{
    try {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (current_.get() != call) return;
            graveyard_.push_back(std::move(current_));
        }
        condition_.notify_all();
    } catch (...) {
        // Se a alocação do graveyard falhar, mantém a chamada corrente viva.
    }
}

void CallRegistry::retireCurrent() noexcept
{
    try {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (current_ == nullptr) return;
            graveyard_.push_back(std::move(current_));
        }
        condition_.notify_all();
    } catch (...) {
        // Preserva current_ se o graveyard não puder crescer.
    }
}

std::size_t CallRegistry::reap() noexcept
{
    std::vector<std::unique_ptr<SipCall>> ready;
    try {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready.reserve(graveyard_.size());
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto iterator = graveyard_.begin();
            while (iterator != graveyard_.end()) {
                if ((*iterator)->canReap()) {
                    ready.push_back(std::move(*iterator));
                    iterator = graveyard_.erase(iterator);
                } else {
                    ++iterator;
                }
            }
        }
        const std::size_t count = ready.size();
        ready.clear();
        return count;
    } catch (...) {
        return 0U;
    }
}

std::size_t CallRegistry::retiredCount() const noexcept
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        return graveyard_.size();
    } catch (...) {
        return 0U;
    }
}

bool CallRegistry::waitUntilIdle(std::chrono::milliseconds timeout) noexcept
{
    try {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, timeout, [this] { return current_ == nullptr; });
    } catch (...) {
        return false;
    }
}

bool CallRegistry::waitUntilSafeToReap(std::chrono::milliseconds timeout) noexcept
{
    try {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, timeout, [this] {
            return std::all_of(
                graveyard_.cbegin(), graveyard_.cend(),
                [](const std::unique_ptr<SipCall>& call) { return call->canReap(); });
        });
    } catch (...) {
        return false;
    }
}

void CallRegistry::notifyCallbackComplete() noexcept
{
    condition_.notify_all();
}

} // namespace polphone::sip
