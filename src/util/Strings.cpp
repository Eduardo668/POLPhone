/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "util/Strings.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>

namespace polphone::util {
namespace {

bool isSpace(char value) noexcept
{
    return std::isspace(static_cast<unsigned char>(value)) != 0;
}

char toLowerAscii(char value) noexcept
{
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

bool isAsciiDigit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

} // namespace

std::string trim(std::string_view value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return std::string(first, last);
}

std::vector<std::string> split(std::string_view value, char delimiter, bool keepEmpty)
{
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t end = value.find(delimiter, begin);
        const std::size_t length = end == std::string_view::npos ? value.size() - begin : end - begin;
        if (keepEmpty || length != 0) {
            parts.emplace_back(value.substr(begin, length));
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return parts;
}

bool startsWith(std::string_view value, std::string_view prefix) noexcept
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool iequals(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (toLowerAscii(left[index]) != toLowerAscii(right[index])) {
            return false;
        }
    }
    return true;
}

std::string maskMiddle(std::string_view value, std::size_t keepLast)
{
    std::size_t digitCount = 0;
    for (const char character : value) {
        if (isAsciiDigit(character)) {
            ++digitCount;
        }
    }

    const std::size_t digitsToMask = digitCount > keepLast ? digitCount - keepLast : 0;
    std::size_t digitsVisited = 0;
    std::string masked(value);
    for (char& character : masked) {
        if (isAsciiDigit(character)) {
            if (digitsVisited < digitsToMask) {
                character = '*';
            }
            ++digitsVisited;
        }
    }
    return masked;
}

bool toUtf8Console() noexcept
{
    const bool outputConfigured = SetConsoleOutputCP(CP_UTF8) != 0;
    const bool inputConfigured = SetConsoleCP(CP_UTF8) != 0;
    return outputConfigured && inputConfigured;
}

} // namespace polphone::util
