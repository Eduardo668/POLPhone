/* POLPhone - regras de apresentação independentes de WinUI. GPL-2.0-only. */

#include "core/Presentation.h"

#include "logging/Redactor.h"
#include "util/Strings.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>

namespace polphone::core {
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

CommandAvailability commandAvailability(
    const TelephonySnapshot& snapshot,
    bool callCommandPending,
    bool hangupCommandPending) noexcept
{
    CommandAvailability available;
    const bool ready = snapshot.application == ApplicationState::Ready;
    const bool idle = snapshot.call == CallState::Idle || snapshot.call == CallState::Failed;
    available.registerAccount = ready
        && (snapshot.registration == RegistrationState::Disconnected
            || snapshot.registration == RegistrationState::Failed);
    available.unregisterAccount = ready
        && snapshot.registration == RegistrationState::Connected;
    available.makeCall = ready && snapshot.registration == RegistrationState::Connected
        && idle && !callCommandPending;
    available.answerCall = ready && snapshot.call == CallState::Incoming && !callCommandPending;
    available.rejectCall = ready && snapshot.call == CallState::Incoming && !hangupCommandPending;
    available.hangupCall = ready
        && snapshot.call != CallState::Idle && snapshot.call != CallState::Failed
        && snapshot.call != CallState::Ending && !hangupCommandPending;
    available.mute = ready && snapshot.call == CallState::Confirmed;
    available.keypad = ready && snapshot.call == CallState::Confirmed;
    available.sendDtmf = available.keypad && snapshot.media == MediaState::Active
        && !snapshot.dtmfInFlight;
    return available;
}

void PhoneViewModel::update(TelephonySnapshot snapshot)
{
    snapshot_ = std::move(snapshot);
    if (snapshot_.call != CallState::Calling && snapshot_.call != CallState::Connecting) {
        callPending_ = false;
    }
    if (snapshot_.call != CallState::Ending) hangupPending_ = false;
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
