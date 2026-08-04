/* POLPhone - regras de apresentação independentes de WinUI. GPL-2.0-only. */

#include "core/Presentation.h"

#include "logging/Redactor.h"
#include "util/Strings.h"

#include <algorithm>
#include <cctype>
#include <tuple>
#include <iomanip>
#include <sstream>
#include <utility>

namespace polphone::core {
namespace {

std::string trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1U));
}

std::string unquote(std::string value)
{
    if (value.size() >= 2U && value.front() == '"' && value.back() == '"') {
        value.erase(value.begin());
        value.pop_back();
    }
    return trim(value);
}

} // namespace
namespace {

bool isDialCharacter(char value) noexcept
{
    return (value >= '0' && value <= '9') || value == '*' || value == '#'
        || value == '+';
}

} // namespace

util::Result<std::string> validateDestination(std::string_view value)
{
    const std::string trimmed = util::trim(value);
    if (trimmed.empty()) {
        return util::Result<std::string>::failure(
            util::ErrorCode::Validation, "Informe um número ou endereço SIP.", "destination");
    }
    if (trimmed.size() > 255U) {
        return util::Result<std::string>::failure(
            util::ErrorCode::Validation, "O destino é muito longo.", "destination");
    }
    if (util::startsWith(trimmed, "sip:") || util::startsWith(trimmed, "sips:")
        || util::startsWith(trimmed, "SIP:") || util::startsWith(trimmed, "SIPS:")) {
        if (trimmed.find('@') == std::string::npos || trimmed.find(' ') != std::string::npos) {
            return util::Result<std::string>::failure(
                util::ErrorCode::Validation,
                "Use um endereço SIP completo, por exemplo sip:1001@servidor.",
                "destination");
        }
        return util::Result<std::string>::success(trimmed);
    }
    if (!std::all_of(trimmed.begin(), trimmed.end(), isDialCharacter)) {
        return util::Result<std::string>::failure(
            util::ErrorCode::Validation,
            "Use somente números, +, * ou #, ou informe uma URI SIP completa.",
            "destination");
    }
    return util::Result<std::string>::success(trimmed);
}

std::string maskDestination(std::string_view value)
{
    return logging::Redactor::redact(value);
}

std::string formatCallDuration(std::uint64_t seconds)
{
    const auto hours = seconds / 3600U;
    const auto minutes = (seconds % 3600U) / 60U;
    const auto remainder = seconds % 60U;
    std::ostringstream output;
    if (hours != 0U) output << std::setfill('0') << std::setw(2) << hours << ':';
    output << std::setfill('0') << std::setw(2) << minutes << ':'
           << std::setw(2) << remainder;
    return output.str();
}

CallerIdentity parseCallerIdentity(std::string_view value)
{
    CallerIdentity result;
    result.sanitizedUri = trim(value);

    std::string_view uri = value;
    const auto open = value.find('<');
    const auto close = open == std::string_view::npos
        ? std::string_view::npos : value.find('>', open + 1U);
    if (open != std::string_view::npos && close != std::string_view::npos) {
        result.displayName = unquote(trim(value.substr(0U, open)));
        uri = value.substr(open + 1U, close - open - 1U);
    }

    const auto colon = uri.find(':');
    const auto userStart = colon == std::string_view::npos ? 0U : colon + 1U;
    const auto at = uri.find('@', userStart);
    const auto end = at == std::string_view::npos
        ? uri.find_first_of(";?>", userStart) : at;
    if (end != std::string_view::npos && end > userStart) {
        result.number = trim(uri.substr(userStart, end - userStart));
    }
    if (result.number.empty()) result.number = result.sanitizedUri;
    if (result.displayName.empty()) result.displayName = result.number;
    return result;
}

bool applyPhoneEvent(TelephonySnapshot& snapshot, const PhoneEvent& event)
{
    const auto before = std::make_tuple(snapshot.registration,
                                        snapshot.call,
                                        snapshot.callDirection,
                                        snapshot.media,
                                        snapshot.remoteDisplayName,
                                        snapshot.remoteNumber,
                                        snapshot.maskedRemote,
                                        snapshot.muted,
                                        snapshot.callDurationSeconds);
    switch (event.type) {
    case PhoneEventType::RegistrationStarted:
        snapshot.registration = RegistrationState::Connecting;
        break;
    case PhoneEventType::RegistrationSucceeded:
        snapshot.registration = RegistrationState::Connected;
        break;
    case PhoneEventType::RegistrationFailed:
        snapshot.registration = RegistrationState::Failed;
        break;
    case PhoneEventType::IncomingCallReceived:
    {
        const bool idleLike = snapshot.call == CallState::Idle
            || snapshot.call == CallState::Disconnected
            || snapshot.call == CallState::Error;
        const bool repeatedSameCall = snapshot.call == CallState::IncomingRinging
            && snapshot.callDirection == CallDirection::Incoming
            && (event.remoteUri.empty() || snapshot.maskedRemote == event.remoteUri);
        if (!idleLike && !repeatedSameCall) {
            break;
        }
        snapshot.call = CallState::IncomingRinging;
        snapshot.callDirection = CallDirection::Incoming;
        snapshot.remoteDisplayName = event.remoteDisplayName;
        snapshot.remoteNumber = event.remoteNumber;
        snapshot.maskedRemote = event.remoteUri;
        snapshot.media = MediaState::Inactive;
        snapshot.muted = false;
        snapshot.callDurationSeconds = 0;
        break;
    }
    case PhoneEventType::OutgoingCallStarted:
        if (snapshot.call == CallState::Idle
            || snapshot.call == CallState::Disconnected
            || snapshot.call == CallState::Error) {
            snapshot.call = CallState::OutgoingDialing;
            snapshot.callDirection = CallDirection::Outgoing;
            snapshot.remoteDisplayName = event.remoteDisplayName;
            snapshot.remoteNumber = event.remoteNumber;
            snapshot.maskedRemote = event.remoteUri;
            snapshot.media = MediaState::Inactive;
            snapshot.muted = false;
            snapshot.callDurationSeconds = 0;
        }
        break;
    case PhoneEventType::RemoteRinging:
        if (snapshot.callDirection == CallDirection::Incoming
            && snapshot.call == CallState::IncomingRinging) {
            snapshot.call = CallState::IncomingRinging;
        } else if (snapshot.callDirection == CallDirection::Outgoing
                   && (snapshot.call == CallState::OutgoingDialing
                       || snapshot.call == CallState::OutgoingRinging)) {
            snapshot.call = CallState::OutgoingRinging;
        }
        break;
    case PhoneEventType::CallAnswered:
        if (snapshot.call == CallState::IncomingRinging
            || snapshot.call == CallState::OutgoingDialing
            || snapshot.call == CallState::OutgoingRinging) {
            snapshot.call = CallState::Connecting;
        }
        break;
    case PhoneEventType::MediaActivated:
        if (snapshot.call == CallState::Connecting
            || snapshot.call == CallState::Active) {
            snapshot.call = CallState::Active;
            snapshot.media = MediaState::Active;
        }
        break;
    case PhoneEventType::CallRejected:
        if (snapshot.call == CallState::IncomingRinging) {
            snapshot.call = CallState::Disconnecting;
        }
        break;
    case PhoneEventType::CallDisconnected:
        snapshot.call = CallState::Disconnected;
        snapshot.media = MediaState::Inactive;
        snapshot.muted = false;
        snapshot.callDurationSeconds = 0;
        break;
    case PhoneEventType::CallFailed:
        snapshot.call = CallState::Error;
        snapshot.media = MediaState::Failed;
        snapshot.muted = false;
        break;
    case PhoneEventType::LocalMuteChanged:
        if (snapshot.call == CallState::Active) snapshot.muted = event.muted;
        break;
    }
    const auto after = std::make_tuple(snapshot.registration,
                                       snapshot.call,
                                       snapshot.callDirection,
                                       snapshot.media,
                                       snapshot.remoteDisplayName,
                                       snapshot.remoteNumber,
                                       snapshot.maskedRemote,
                                       snapshot.muted,
                                       snapshot.callDurationSeconds);
    if (before == after) return false;
    ++snapshot.revision;
    return true;
}

CommandAvailability commandAvailability(
    const TelephonySnapshot& snapshot,
    bool callCommandPending,
    bool hangupCommandPending) noexcept
{
    CommandAvailability available;
    const bool ready = snapshot.application == ApplicationState::Ready;
    const bool idle = snapshot.call == CallState::Idle
        || snapshot.call == CallState::Disconnected || snapshot.call == CallState::Error;
    available.registerAccount = ready
        && (snapshot.registration == RegistrationState::Disconnected
            || snapshot.registration == RegistrationState::Failed);
    available.unregisterAccount = ready
        && snapshot.registration == RegistrationState::Connected;
    available.makeCall = ready && snapshot.registration == RegistrationState::Connected
        && idle && !callCommandPending;
    available.answerCall = ready && snapshot.call == CallState::IncomingRinging
        && snapshot.callDirection == CallDirection::Incoming && !callCommandPending;
    available.rejectCall = ready && snapshot.call == CallState::IncomingRinging
        && snapshot.callDirection == CallDirection::Incoming && !hangupCommandPending;
    available.hangupCall = ready
        && snapshot.call != CallState::Idle && snapshot.call != CallState::Disconnected
        && snapshot.call != CallState::Error && snapshot.call != CallState::Disconnecting
        && !hangupCommandPending;
    available.mute = ready && snapshot.call == CallState::Active;
    available.keypad = ready && snapshot.call == CallState::Active;
    available.sendDtmf = available.keypad && snapshot.media == MediaState::Active
        && !snapshot.dtmfInFlight;
    return available;
}

void PhoneViewModel::update(TelephonySnapshot snapshot)
{
    snapshot_ = std::move(snapshot);
    if (snapshot_.call != CallState::OutgoingDialing
        && snapshot_.call != CallState::OutgoingRinging
        && snapshot_.call != CallState::Connecting) {
        callPending_ = false;
    }
    if (snapshot_.call != CallState::Disconnecting) hangupPending_ = false;
    if (!snapshot_.dtmfInFlight) dtmfPending_ = false;
}

const TelephonySnapshot& PhoneViewModel::snapshot() const noexcept { return snapshot_; }

CommandAvailability PhoneViewModel::commands() const noexcept
{
    auto result = commandAvailability(snapshot_, callPending_, hangupPending_);
    result.sendDtmf = result.sendDtmf && !dtmfPending_;
    return result;
}

bool PhoneViewModel::beginCall(std::string_view destination)
{
    if (!commands().makeCall) return false;
    auto validated = validateDestination(destination);
    if (!validated) {
        validationMessage_ = validated.error().message;
        return false;
    }
    validationMessage_.clear();
    destination_ = std::move(validated).value();
    callPending_ = true;
    return true;
}

void PhoneViewModel::completeCallCommand() noexcept { callPending_ = false; }

bool PhoneViewModel::beginHangup() noexcept
{
    if (!commands().hangupCall) return false;
    hangupPending_ = true;
    return true;
}

void PhoneViewModel::completeHangupCommand() noexcept { hangupPending_ = false; }

bool PhoneViewModel::beginDtmf() noexcept
{
    if (!commands().sendDtmf) return false;
    dtmfPending_ = true;
    return true;
}

void PhoneViewModel::completeDtmf() noexcept { dtmfPending_ = false; }

void PhoneViewModel::setDtmfMethod(DtmfMethod method) noexcept
{
    selectedMethod_ = method;
    if (ivrMode_ && method != DtmfMethod::Inband) ivrMode_ = false;
}

DtmfMethod PhoneViewModel::selectedDtmfMethod() const noexcept { return selectedMethod_; }

void PhoneViewModel::setIvrMode(bool enabled) noexcept
{
    if (enabled == ivrMode_) return;
    if (enabled) {
        methodBeforeIvr_ = selectedMethod_;
        selectedMethod_ = DtmfMethod::Inband;
    } else {
        selectedMethod_ = methodBeforeIvr_;
    }
    ivrMode_ = enabled;
}

bool PhoneViewModel::ivrMode() const noexcept { return ivrMode_; }

std::string_view PhoneViewModel::ivrStatusText() const noexcept
{
    return ivrMode_ ? "Modo URA ativo" : "Modo URA inativo";
}

const std::string& PhoneViewModel::destination() const noexcept { return destination_; }
const std::string& PhoneViewModel::validationMessage() const noexcept { return validationMessage_; }

} // namespace polphone::core
