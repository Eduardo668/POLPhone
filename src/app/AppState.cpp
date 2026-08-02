/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "app/AppState.h"

#include <utility>

namespace polphone::app {

std::string_view registrationStateName(RegistrationState state) noexcept
{
    switch (state) {
    case RegistrationState::Unconfigured: return "UNCONFIGURED";
    case RegistrationState::Registering: return "REGISTERING";
    case RegistrationState::Registered: return "REGISTERED";
    case RegistrationState::Failed: return "FAILED";
    case RegistrationState::Unregistering: return "UNREGISTERING";
    case RegistrationState::Unregistered: return "UNREGISTERED";
    }
    return "UNKNOWN";
}

std::string_view callStateName(CallState state) noexcept
{
    switch (state) {
    case CallState::Idle: return "IDLE";
    case CallState::Incoming: return "INCOMING";
    case CallState::Calling: return "CALLING";
    case CallState::Early: return "EARLY";
    case CallState::Connecting: return "CONNECTING";
    case CallState::Confirmed: return "CONFIRMED";
    case CallState::Disconnected: return "DISCONNECTED";
    }
    return "UNKNOWN";
}

void AppState::updateRegistration(RegistrationSnapshot snapshot)
{
    std::lock_guard<std::mutex> lock(mutex_);
    registration_ = std::move(snapshot);
}

RegistrationSnapshot AppState::registration() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return registration_;
}

void AppState::updateCall(CallSnapshot snapshot)
{
    std::lock_guard<std::mutex> lock(mutex_);
    call_ = std::move(snapshot);
}

CallSnapshot AppState::call() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return call_;
}

} // namespace polphone::app
