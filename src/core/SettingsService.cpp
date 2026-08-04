/* POLPhone - gravação validada de configurações da GUI. GPL-2.0-only. */

#include "core/SettingsService.h"

#include "config/AppConfig.h"
#include "config/ConfigLoader.h"
#include "config/ConfigValidator.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <system_error>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace polphone::core {
namespace {

nlohmann::json serialize(const config::AppConfig& value)
{
    return {
        {"sip", {
            {"idUri", value.sip.idUri}, {"registrarUri", value.sip.registrarUri},
            {"realm", value.sip.realm}, {"username", value.sip.username},
            {"password", value.sip.password}, {"domain", value.sip.domain},
            {"proxyUri", value.sip.proxyUri}, {"regTimeoutSec", value.sip.regTimeoutSec},
            {"regRetryIntervalSec", value.sip.regRetryIntervalSec},
            {"registerOnStartup", value.sip.registerOnStartup}}},
        {"network", {
            {"localPort", value.network.localPort}, {"boundAddress", value.network.boundAddress},
            {"transport", value.network.transport}}},
        {"audio", {
            {"captureDevice", value.audio.captureDevice},
            {"playbackDevice", value.audio.playbackDevice}, {"clockRate", value.audio.clockRate},
            {"channelCount", value.audio.channelCount}, {"ptimeMs", value.audio.ptimeMs},
            {"ecTailMs", value.audio.ecTailMs}, {"quality", value.audio.quality},
            {"noVad", value.audio.noVad}}},
        {"codecs", {{"priority", value.codecs.priority}}},
        {"dtmf", {
            {"defaultMethod", value.dtmf.defaultMethod}, {"durationMs", value.dtmf.durationMs},
            {"gapMs", value.dtmf.gapMs}, {"volumeDbm0", value.dtmf.volumeDbm0},
            {"localFeedback", value.dtmf.localFeedback}, {"logDigits", value.dtmf.logDigits}}},
        {"logging", {
            {"consoleLevel", value.logging.consoleLevel}, {"fileLevel", value.logging.fileLevel},
            {"directory", value.logging.directory}, {"maxFileMB", value.logging.maxFileMB},
            {"sipMessageTrace", value.logging.sipMessageTrace}}}
    };
}

util::Result<void> replaceFile(const std::filesystem::path& temporary,
                               const std::filesystem::path& destination)
{
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return util::Result<void>::failure(
            util::ErrorCode::Io,
            "Não foi possível substituir o arquivo de configuração.",
            "Win32=" + std::to_string(code));
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return util::Result<void>::failure(
            util::ErrorCode::Io, "Não foi possível substituir o arquivo de configuração.");
    }
#endif
    return util::Result<void>::success();
}

} // namespace

util::Result<void> saveGuiSettings(const std::filesystem::path& path,
                                   const GuiSettings& settings)
{
    config::AppConfig config;
    std::error_code existsError;
    if (std::filesystem::exists(path, existsError)) {
        auto loaded = config::ConfigLoader::load(path);
        if (!loaded) return util::Result<void>::failure(loaded.error());
        config = std::move(loaded.value());
    }

    config.sip.proxyUri = settings.serverSip;
    config.sip.registrarUri = settings.registrarUri;
    config.sip.idUri = settings.idUri;
    config.sip.username = settings.username;
    config.sip.password = settings.password;
    config.sip.domain = settings.domain;
    config.sip.registerOnStartup = settings.registerOnStartup;
    config.network.transport = "udp";
    config.audio.captureDevice = settings.captureDevice;
    config.audio.playbackDevice = settings.playbackDevice;
    config.dtmf.defaultMethod = settings.dtmfMethod;
    config.dtmf.durationMs = settings.dtmfDurationMs;
    config.dtmf.gapMs = settings.dtmfGapMs;
    config.dtmf.volumeDbm0 = settings.inbandVolumeDbm0;
    config.logging.consoleLevel = settings.logLevel;
    config.logging.fileLevel = settings.logLevel;

    const auto validated = config::ConfigValidator::validate(config);
    if (!validated) return util::Result<void>::failure(validated.error());

    std::error_code directoryError;
    const auto parent = path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, directoryError);
    if (directoryError) {
        return util::Result<void>::failure(
            util::ErrorCode::Io,
            "Não foi possível criar o diretório da configuração.",
            parent.u8string());
    }

    auto temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return util::Result<void>::failure(
            util::ErrorCode::Io,
            "Não foi possível abrir o arquivo temporário de configuração.",
            temporary.u8string());
    }
    output << serialize(config).dump(2) << '\n';
    output.flush();
    if (!output) {
        output.close();
        std::filesystem::remove(temporary, directoryError);
        return util::Result<void>::failure(
            util::ErrorCode::Io,
            "Falha ao gravar a configuração; o arquivo anterior foi preservado.",
            temporary.u8string());
    }
    output.close();
    return replaceFile(temporary, path);
}

} // namespace polphone::core
