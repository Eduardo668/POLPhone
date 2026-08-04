/* POLPhone - fábricas públicas do controlador. GPL-2.0-only. */

#include "core/ControllerFactory.h"

#include "core/MockTelephonyBackend.h"
#include "core/RealTelephonyBackend.h"

namespace polphone::core {

std::unique_ptr<PolPhoneController> createDemoController()
{
    return std::make_unique<PolPhoneController>(
        std::make_unique<MockTelephonyBackend>());
}

std::unique_ptr<PolPhoneController> createRealController(
    const std::filesystem::path& configPath)
{
    return std::make_unique<PolPhoneController>(
        std::make_unique<RealTelephonyBackend>(configPath));
}

} // namespace polphone::core
