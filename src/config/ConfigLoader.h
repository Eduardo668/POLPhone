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

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace polphone::config {

class ConfigLoader final {
public:
    using Warnings = std::vector<std::string>;

    [[nodiscard]] static util::Result<AppConfig> load(
        const std::filesystem::path& path,
        Warnings* warnings = nullptr);

    [[nodiscard]] static util::Result<AppConfig> parse(
        std::string_view document,
        Warnings* warnings = nullptr);
};

} // namespace polphone::config
