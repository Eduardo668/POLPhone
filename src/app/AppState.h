/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include <mutex>
#include <string>
#include <string_view>

namespace polphone::app {

enum class RegistrationState {
    Unconfigured,
    Registering,
    Registered,
    Failed,
    Unregistering,
    Unregistered
};

enum class CallState {
    Idle,
    Incoming,
    Calling,
    Early,
    Connecting,
    Confirmed,
    Disconnected
};

enum class CallDirection {
    None,
    Incoming,
    Outgoing
};

[[nodiscard]] std::string_view registrationStateName(RegistrationState state) noexcept;
[[nodiscard]] std::string_view callStateName(CallState state) noexcept;
[[nodiscard]] std::string_view callDirectionName(CallDirection direction) noexcept;

struct RegistrationSnapshot {
    RegistrationState state{RegistrationState::Unconfigured};
    bool active{false};
    int sipCode{0};
    unsigned expiresSec{0};
    std::string reason;
    std::string message;
};

struct CallSnapshot {
    CallState state{CallState::Idle};
    CallDirection direction{CallDirection::None};
    int sipCode{0};
    std::string reason;
    std::string remoteUri;
};

class AppState final {
public:
    void updateRegistration(RegistrationSnapshot snapshot);
    [[nodiscard]] RegistrationSnapshot registration() const;
    void updateCall(CallSnapshot snapshot);
    [[nodiscard]] CallSnapshot call() const;

private:
    mutable std::mutex mutex_;
    RegistrationSnapshot registration_;
    CallSnapshot call_;
};

} // namespace polphone::app
