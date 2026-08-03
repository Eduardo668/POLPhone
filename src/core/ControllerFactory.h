/* POLPhone - fábricas públicas do controlador. GPL-2.0-only. */

#pragma once

#include "core/PolPhoneController.h"

#include <filesystem>
#include <memory>

namespace polphone::core {

[[nodiscard]] POLPHONE_CORE_API std::unique_ptr<PolPhoneController>
    createDemoController();
[[nodiscard]] POLPHONE_CORE_API std::unique_ptr<PolPhoneController>
    createRealController(const std::filesystem::path& configPath);

} // namespace polphone::core
