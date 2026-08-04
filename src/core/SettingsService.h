/* POLPhone - gravação validada de configurações da GUI. GPL-2.0-only. */

#pragma once

#include "core/CoreApi.h"
#include "util/Result.h"

#include <filesystem>
#include <string>

namespace polphone::core {

struct GuiSettings {
    std::string serverSip;
    std::string registrarUri;
    std::string idUri;
    std::string username;
    std::string password;
    std::string domain;
    bool registerOnStartup{true};
    std::string captureDevice;
    std::string playbackDevice;
    std::string dtmfMethod{"rfc4733"};
    int dtmfDurationMs{160};
    int dtmfGapMs{100};
    int inbandVolumeDbm0{-10};
    int logLevel{4};
};

// Valida com ConfigValidator e somente então substitui o arquivo de destino.
// A senha integra o documento persistido, mas nunca aparece em erros ou logs.
[[nodiscard]] POLPHONE_CORE_API util::Result<void> saveGuiSettings(
    const std::filesystem::path& path,
    const GuiSettings& settings);

} // namespace polphone::core
