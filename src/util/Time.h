/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include <cstdint>
#include <string>

namespace polphone::util {

[[nodiscard]] std::string iso8601Now();
[[nodiscard]] std::uint64_t monotonicMilliseconds() noexcept;

} // namespace polphone::util
