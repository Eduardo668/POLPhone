/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace polphone::app {

enum class UiEventSeverity {
    Info,
    Warning,
    Error
};

struct UiEvent {
    UiEventSeverity severity{UiEventSeverity::Info};
    std::string category;
    std::string text;
};

class EventQueue final {
public:
    [[nodiscard]] bool push(UiEvent event) noexcept
    {
        try {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                events_.push_back(std::move(event));
            }
            condition_.notify_one();
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] std::vector<UiEvent> drain()
    {
        std::deque<UiEvent> pending;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending.swap(events_);
        }
        return std::vector<UiEvent>(
            std::make_move_iterator(pending.begin()),
            std::make_move_iterator(pending.end()));
    }

    template <typename Rep, typename Period>
    [[nodiscard]] bool waitForEvent(const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, timeout, [this] { return !events_.empty(); });
    }

    [[nodiscard]] std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<UiEvent> events_;
};

} // namespace polphone::app
