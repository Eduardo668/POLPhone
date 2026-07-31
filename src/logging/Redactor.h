/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include <string>
#include <string_view>

namespace polphone::logging {

class Redactor final {
public:
    [[nodiscard]] static std::string redact(std::string_view input,
                                            bool logDtmfDigits = false);
};

} // namespace polphone::logging
