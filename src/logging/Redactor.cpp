/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "logging/Redactor.h"

#include "util/Strings.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <utility>

namespace polphone::logging {
namespace {

const std::regex kAuthorization(
    R"regex(((?:Proxy-Authorization|Authorization)[ \t]*:)[^\r\n]*)regex",
    std::regex_constants::icase);
const std::regex kSensitiveKey(
    R"regex(("?(?:senha|password|passwd|secret|token)"?[ \t]*[:=][ \t]*)(?:"[^"\r\n]*"|'[^'\r\n]*'|[^ \t,;}\r\n]+))regex",
    std::regex_constants::icase);
const std::regex kDigestAttribute(
    R"regex(((?:response|nonce|cnonce)[ \t]*=[ \t]*)(?:"[^"\r\n]*"|[^, \t\r\n]+))regex",
    std::regex_constants::icase);
const std::regex kDtmfValue(
    R"regex(((?:Signal|digit)[ \t]*=[ \t]*)(?:"?[^,; \t\r\n"]+"?))regex",
    std::regex_constants::icase);
const std::regex kSipUri(
    R"regex((sips?:)([^@<> \t\r\n;]+)(@[^<> \t\r\n;]+))regex",
    std::regex_constants::icase);
const std::regex kContextualPhone(
    R"regex(((?:destino|destination|numero|number|telefone|phone|discado|dialed)[ \t]*[:=]?[ \t]*)(\+?[0-9]{6,}))regex",
    std::regex_constants::icase);
const std::regex kBarePhone(R"regex(^[ \t]*(\+?[0-9]{6,})[ \t]*$)regex");
const std::regex kPreservedHeader(
    R"regex((^|[ \t])(?:Call-ID|User-Agent)[ \t]*:)regex",
    std::regex_constants::icase);

std::string regexReplace(const std::string& input,
                         const std::regex& expression,
                         const std::string& replacement)
{
    return std::regex_replace(input, expression, replacement);
}

bool isDigit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

char lowerAscii(char value) noexcept
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

std::string lowerCopy(std::string_view input)
{
    std::string lowered(input);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), lowerAscii);
    return lowered;
}

std::string redactMachinePaths(std::string line)
{
    // Quando o caminho aponta para este checkout, preserva apenas o trecho
    // relativo a POLPhone. Isso mantém arquivo/linha úteis sem expor usuário.
    std::size_t searchFrom = 0;
    for (;;) {
        const std::string lowered = lowerCopy(line);
        const std::size_t backslashProject = lowered.find("polphone\\", searchFrom);
        const std::size_t slashProject = lowered.find("polphone/", searchFrom);
        const std::size_t project = std::min(backslashProject, slashProject);
        if (project == std::string::npos) {
            break;
        }
        const std::size_t tokenDelimiter = line.find_last_of(" \t", project);
        const std::size_t tokenStart = tokenDelimiter == std::string::npos ? 0U : tokenDelimiter + 1U;
        const std::string prefix = lowered.substr(tokenStart, project - tokenStart);
        if (prefix.find("wsl.localhost") == std::string::npos
            && prefix.find("\\home\\") == std::string::npos
            && prefix.find("/home/") == std::string::npos
            && prefix.find("\\users\\") == std::string::npos) {
            searchFrom = project + std::string_view("polphone").size();
            continue;
        }
        line.erase(tokenStart, project - tokenStart);
        searchFrom = tokenStart + std::string_view("polphone").size();
    }

    const auto redactUserAfter = [&line](std::string_view marker, char separator) {
        for (;;) {
            const std::string lowered = lowerCopy(line);
            const std::size_t markerPosition = lowered.find(marker);
            if (markerPosition == std::string::npos) {
                break;
            }
            const std::size_t userStart = markerPosition + marker.size();
            const std::size_t userEnd = line.find(separator, userStart);
            if (userEnd == std::string::npos || line.substr(userStart, userEnd - userStart) == "[USER]") {
                break;
            }
            line.replace(userStart, userEnd - userStart, "[USER]");
        }
    };
    redactUserAfter("\\users\\", '\\');
    redactUserAfter("\\home\\", '\\');
    redactUserAfter("/home/", '/');
    return line;
}

std::string maskSipUser(std::string user)
{
    if (user.find('*') != std::string::npos || user == "[REDACTED]") {
        return user;
    }

    const std::size_t digits = static_cast<std::size_t>(
        std::count_if(user.begin(), user.end(), isDigit));
    const bool numericUser = digits != 0U
        && std::all_of(user.begin(), user.end(), [](char value) {
               return isDigit(value) || value == '+' || value == '-' || value == '.';
           });
    if (numericUser && digits <= 5U) {
        return user;
    }
    if (numericUser) {
        return util::maskMiddle(user, 4U);
    }

    std::transform(user.begin(), user.end(), user.begin(), [](char value) {
        return std::isalnum(static_cast<unsigned char>(value)) != 0 ? '*' : value;
    });
    return user;
}

std::string redactSipUris(const std::string& input)
{
    std::string output;
    std::size_t consumed = 0;
    for (std::sregex_iterator match(input.begin(), input.end(), kSipUri), end;
         match != end;
         ++match) {
        const auto position = static_cast<std::size_t>(match->position());
        output.append(input, consumed, position - consumed);
        output.append((*match)[1].str());
        output.append(maskSipUser((*match)[2].str()));
        output.append((*match)[3].str());
        consumed = position + static_cast<std::size_t>(match->length());
    }
    output.append(input, consumed, std::string::npos);
    return output;
}

std::string redactContextualPhones(const std::string& input)
{
    std::string output;
    std::size_t consumed = 0;
    for (std::sregex_iterator match(input.begin(), input.end(), kContextualPhone), end;
         match != end;
         ++match) {
        const auto position = static_cast<std::size_t>(match->position());
        output.append(input, consumed, position - consumed);
        output.append((*match)[1].str());
        output.append(util::maskMiddle((*match)[2].str(), 4U));
        consumed = position + static_cast<std::size_t>(match->length());
    }
    output.append(input, consumed, std::string::npos);

    std::smatch bare;
    if (std::regex_match(output, bare, kBarePhone)) {
        return util::maskMiddle(output, 4U);
    }
    return output;
}

std::string redactLine(std::string line, bool logDtmfDigits)
{
    if (std::regex_search(line, kPreservedHeader)) {
        return line;
    }

    line = regexReplace(line, kAuthorization, "$1 [REDACTED]");
    line = regexReplace(line, kSensitiveKey, "$1\"[REDACTED]\"");
    line = regexReplace(line, kDigestAttribute, "$1\"[REDACTED]\"");
    if (!logDtmfDigits) {
        line = regexReplace(line, kDtmfValue, "$1*");
    }
    line = redactSipUris(line);
    line = redactContextualPhones(line);
    return redactMachinePaths(std::move(line));
}

} // namespace

std::string Redactor::redact(std::string_view input, bool logDtmfDigits)
{
    if (input.empty()) {
        return {};
    }

    std::string output;
    std::size_t begin = 0;
    while (begin <= input.size()) {
        const std::size_t end = input.find('\n', begin);
        std::string line(input.substr(
            begin, end == std::string_view::npos ? input.size() - begin : end - begin));
        const bool hadCarriageReturn = !line.empty() && line.back() == '\r';
        if (hadCarriageReturn) {
            line.pop_back();
        }
        output.append(redactLine(std::move(line), logDtmfDigits));
        if (hadCarriageReturn) {
            output.push_back('\r');
        }
        if (end == std::string_view::npos) {
            break;
        }
        output.push_back('\n');
        begin = end + 1U;
    }
    return output;
}

} // namespace polphone::logging
