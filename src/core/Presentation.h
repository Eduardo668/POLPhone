/* POLPhone - regras de apresentação independentes de WinUI. GPL-2.0-only. */

#pragma once

#include "core/TelephonyTypes.h"
#include "util/Result.h"

#include <chrono>
#include <string>
#include <string_view>

namespace polphone::core {

[[nodiscard]] POLPHONE_CORE_API util::Result<std::string> validateDestination(std::string_view value);
[[nodiscard]] POLPHONE_CORE_API std::string maskDestination(std::string_view value);
[[nodiscard]] POLPHONE_CORE_API std::string formatCallDuration(std::uint64_t seconds);
[[nodiscard]] POLPHONE_CORE_API CommandAvailability commandAvailability(
    const TelephonySnapshot& snapshot,
    bool callCommandPending = false,
    bool hangupCommandPending = false) noexcept;

class POLPHONE_CORE_API PhoneViewModel final {
public:
    void update(TelephonySnapshot snapshot);
    [[nodiscard]] const TelephonySnapshot& snapshot() const noexcept;
    [[nodiscard]] CommandAvailability commands() const noexcept;

    [[nodiscard]] bool beginCall(std::string_view destination);
    void completeCallCommand() noexcept;
    [[nodiscard]] bool beginHangup() noexcept;
    void completeHangupCommand() noexcept;
    [[nodiscard]] bool beginDtmf() noexcept;
    void completeDtmf() noexcept;

    void setDtmfMethod(DtmfMethod method) noexcept;
    [[nodiscard]] DtmfMethod selectedDtmfMethod() const noexcept;
    void setIvrMode(bool enabled) noexcept;
    [[nodiscard]] bool ivrMode() const noexcept;
    [[nodiscard]] std::string_view ivrStatusText() const noexcept;
    [[nodiscard]] const std::string& destination() const noexcept;
    [[nodiscard]] const std::string& validationMessage() const noexcept;

private:
    TelephonySnapshot snapshot_;
    std::string destination_;
    std::string validationMessage_;
    DtmfMethod selectedMethod_{DtmfMethod::Automatic};
    DtmfMethod methodBeforeIvr_{DtmfMethod::Automatic};
    bool ivrMode_{false};
    bool callPending_{false};
    bool hangupPending_{false};
    bool dtmfPending_{false};
};

} // namespace polphone::core
