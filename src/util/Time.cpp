/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "util/Time.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace polphone::util {

std::string iso8601Now()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    if (localtime_s(&localTime, &time) != 0) {
        return {};
    }

    const auto millisecondsSinceEpoch =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const auto milliseconds = static_cast<unsigned>(millisecondsSinceEpoch % 1000);

    const __time64_t localAsUtc = _mkgmtime64(&localTime);
    const long long offsetSeconds =
        static_cast<long long>(localAsUtc) - static_cast<long long>(time);
    const char offsetSign = offsetSeconds < 0 ? '-' : '+';
    const long long absoluteOffset = std::llabs(offsetSeconds);
    const long long offsetHours = absoluteOffset / 3600;
    const long long offsetMinutes = (absoluteOffset % 3600) / 60;

    std::ostringstream output;
    output << std::put_time(&localTime, "%Y-%m-%dT%H:%M:%S")
           << '.' << std::setfill('0') << std::setw(3) << milliseconds
           << offsetSign << std::setw(2) << offsetHours
           << ':' << std::setw(2) << offsetMinutes;
    return output.str();
}

std::uint64_t monotonicMilliseconds() noexcept
{
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

} // namespace polphone::util
