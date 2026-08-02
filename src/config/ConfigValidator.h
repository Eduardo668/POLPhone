/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include "config/AppConfig.h"
#include "util/Result.h"

namespace polphone::config {

class ConfigValidator final {
public:
    [[nodiscard]] static util::Result<void> validate(const AppConfig& config);
};

} // namespace polphone::config
