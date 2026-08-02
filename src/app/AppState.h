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

[[nodiscard]] std::string_view registrationStateName(RegistrationState state) noexcept;

struct RegistrationSnapshot {
    RegistrationState state{RegistrationState::Unconfigured};
    bool active{false};
    int sipCode{0};
    unsigned expiresSec{0};
    std::string reason;
    std::string message;
};

class AppState final {
public:
    void updateRegistration(RegistrationSnapshot snapshot);
    [[nodiscard]] RegistrationSnapshot registration() const;

private:
    mutable std::mutex mutex_;
    RegistrationSnapshot registration_;
};

} // namespace polphone::app
