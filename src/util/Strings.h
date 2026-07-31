/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace polphone::util {

[[nodiscard]] std::string trim(std::string_view value);

// Por padrão, segmentos vazios são descartados. keepEmpty=true os preserva,
// inclusive nas extremidades.
[[nodiscard]] std::vector<std::string> split(std::string_view value,
                                             char delimiter,
                                             bool keepEmpty = false);

[[nodiscard]] bool startsWith(std::string_view value, std::string_view prefix) noexcept;
[[nodiscard]] bool iequals(std::string_view left, std::string_view right) noexcept;

// Mascara todos os dígitos, exceto os keepLast últimos. Caracteres não
// numéricos são preservados para manter o formato da informação.
[[nodiscard]] std::string maskMiddle(std::string_view value, std::size_t keepLast);

// Configura entrada e saída do console do Windows para UTF-8.
[[nodiscard]] bool toUtf8Console() noexcept;

} // namespace polphone::util
