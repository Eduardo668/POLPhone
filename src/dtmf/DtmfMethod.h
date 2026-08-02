/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace polphone::dtmf {

enum class DtmfMethod {
    Rfc4733,
    Inband,
    SipInfo
};

enum class DtmfExecutionPath {
    Rfc4733,
    Inband,
    SipInfo
};

[[nodiscard]] inline std::string_view methodName(DtmfMethod method) noexcept
{
    switch (method) {
    case DtmfMethod::Rfc4733: return "rfc4733";
    case DtmfMethod::Inband: return "inband";
    case DtmfMethod::SipInfo: return "info";
    }
    return "unknown";
}

[[nodiscard]] inline std::optional<DtmfMethod> parseMethod(std::string_view text)
{
    std::string normalized(text);
    for (char& character : normalized) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    if (normalized == "rfc4733" || normalized == "rfc2833") {
        return DtmfMethod::Rfc4733;
    }
    if (normalized == "inband") return DtmfMethod::Inband;
    if (normalized == "info") return DtmfMethod::SipInfo;
    return std::nullopt;
}

[[nodiscard]] inline std::optional<DtmfExecutionPath> executionPathFor(
    DtmfMethod method) noexcept
{
    switch (method) {
    case DtmfMethod::Rfc4733: return DtmfExecutionPath::Rfc4733;
    case DtmfMethod::Inband: return DtmfExecutionPath::Inband;
    case DtmfMethod::SipInfo: return DtmfExecutionPath::SipInfo;
    }
    return std::nullopt;
}

} // namespace polphone::dtmf
