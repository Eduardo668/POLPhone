/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "config/ConfigLoader.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace polphone::config {
namespace {

using Json = nlohmann::json;
using Warnings = ConfigLoader::Warnings;

util::Result<void> parseFailure(std::string path, std::string expected)
{
    return util::Result<void>::failure(
        util::ErrorCode::Parse,
        "Tipo inválido na configuração; informe " + std::move(expected) + ".",
        std::move(path));
}

void warnUnknownFields(const Json& object,
                       const std::set<std::string>& known,
                       std::string_view prefix,
                       Warnings* warnings)
{
    if (warnings == nullptr) {
        return;
    }
    for (auto entry = object.cbegin(); entry != object.cend(); ++entry) {
        if (known.find(entry.key()) == known.end()) {
            const std::string separator = prefix.empty() ? "" : ".";
            warnings->push_back(
                "Campo desconhecido ignorado: " + std::string(prefix) + separator + entry.key());
        }
    }
}

util::Result<const Json*> optionalObject(const Json& parent, std::string_view key)
{
    const auto entry = parent.find(std::string(key));
    if (entry == parent.end()) {
        return util::Result<const Json*>::success(nullptr);
    }
    if (!entry->is_object()) {
        return util::Result<const Json*>::failure(
            util::ErrorCode::Parse,
            "Tipo inválido na configuração; informe um objeto JSON.",
            std::string(key));
    }
    return util::Result<const Json*>::success(&*entry);
}

util::Result<void> readString(const Json& object,
                              std::string_view key,
                              std::string_view path,
                              std::string& target)
{
    const auto entry = object.find(std::string(key));
    if (entry == object.end()) {
        return util::Result<void>::success();
    }
    if (!entry->is_string()) {
        return parseFailure(std::string(path), "uma string");
    }
    target = entry->get<std::string>();
    return util::Result<void>::success();
}

util::Result<void> readBool(const Json& object,
                            std::string_view key,
                            std::string_view path,
                            bool& target)
{
    const auto entry = object.find(std::string(key));
    if (entry == object.end()) {
        return util::Result<void>::success();
    }
    if (!entry->is_boolean()) {
        return parseFailure(std::string(path), "true ou false");
    }
    target = entry->get<bool>();
    return util::Result<void>::success();
}

util::Result<void> readInt(const Json& object,
                           std::string_view key,
                           std::string_view path,
                           int& target)
{
    const auto entry = object.find(std::string(key));
    if (entry == object.end()) {
        return util::Result<void>::success();
    }
    if (!entry->is_number_integer()) {
        return parseFailure(std::string(path), "um número inteiro");
    }

    try {
        if (entry->is_number_unsigned()) {
            const std::uint64_t value = entry->get<std::uint64_t>();
            if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                return parseFailure(std::string(path), "um número inteiro suportado");
            }
            target = static_cast<int>(value);
        } else {
            const std::int64_t value = entry->get<std::int64_t>();
            if (value < static_cast<std::int64_t>(std::numeric_limits<int>::min())
                || value > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
                return parseFailure(std::string(path), "um número inteiro suportado");
            }
            target = static_cast<int>(value);
        }
    } catch (const Json::exception&) {
        return parseFailure(std::string(path), "um número inteiro suportado");
    }
    return util::Result<void>::success();
}

util::Result<void> parseSip(const Json& root, AppConfig& config, Warnings* warnings)
{
    const auto sectionResult = optionalObject(root, "sip");
    if (!sectionResult) {
        return util::Result<void>::failure(sectionResult.error());
    }
    const Json* section = sectionResult.value();
    if (section == nullptr) {
        return util::Result<void>::success();
    }
    warnUnknownFields(*section,
                      {"idUri", "displayName", "registrarUri", "realm", "username",
                       "authUsername", "password", "domain", "proxyUri", "regTimeoutSec",
                       "regRetryIntervalSec", "registerOnStartup"},
                      "sip",
                      warnings);

    if (const auto result = readString(*section, "idUri", "sip.idUri", config.sip.idUri); !result) return result;
    if (const auto result = readString(*section, "displayName", "sip.displayName", config.sip.displayName); !result) return result;
    if (const auto result = readString(*section, "registrarUri", "sip.registrarUri", config.sip.registrarUri); !result) return result;
    if (const auto result = readString(*section, "realm", "sip.realm", config.sip.realm); !result) return result;
    if (const auto result = readString(*section, "username", "sip.username", config.sip.username); !result) return result;
    if (const auto result = readString(*section, "authUsername", "sip.authUsername", config.sip.authUsername); !result) return result;
    if (const auto result = readString(*section, "password", "sip.password", config.sip.password); !result) return result;
    if (const auto result = readString(*section, "domain", "sip.domain", config.sip.domain); !result) return result;
    if (const auto result = readString(*section, "proxyUri", "sip.proxyUri", config.sip.proxyUri); !result) return result;
    if (const auto result = readInt(*section, "regTimeoutSec", "sip.regTimeoutSec", config.sip.regTimeoutSec); !result) return result;
    if (const auto result = readInt(*section, "regRetryIntervalSec", "sip.regRetryIntervalSec", config.sip.regRetryIntervalSec); !result) return result;
    return readBool(*section, "registerOnStartup", "sip.registerOnStartup", config.sip.registerOnStartup);
}

util::Result<void> parseNetwork(const Json& root, AppConfig& config, Warnings* warnings)
{
    const auto sectionResult = optionalObject(root, "network");
    if (!sectionResult) return util::Result<void>::failure(sectionResult.error());
    const Json* section = sectionResult.value();
    if (section == nullptr) return util::Result<void>::success();
    warnUnknownFields(*section, {"localPort", "boundAddress", "transport"}, "network", warnings);

    if (const auto result = readInt(*section, "localPort", "network.localPort", config.network.localPort); !result) return result;
    if (const auto result = readString(*section, "boundAddress", "network.boundAddress", config.network.boundAddress); !result) return result;
    return readString(*section, "transport", "network.transport", config.network.transport);
}

util::Result<void> parseAudio(const Json& root, AppConfig& config, Warnings* warnings)
{
    const auto sectionResult = optionalObject(root, "audio");
    if (!sectionResult) return util::Result<void>::failure(sectionResult.error());
    const Json* section = sectionResult.value();
    if (section == nullptr) return util::Result<void>::success();
    warnUnknownFields(*section,
                      {"captureDevice", "playbackDevice", "clockRate", "channelCount", "ptimeMs",
                       "ecTailMs", "quality", "noVad"},
                      "audio",
                      warnings);

    if (const auto result = readString(*section, "captureDevice", "audio.captureDevice", config.audio.captureDevice); !result) return result;
    if (const auto result = readString(*section, "playbackDevice", "audio.playbackDevice", config.audio.playbackDevice); !result) return result;
    if (const auto result = readInt(*section, "clockRate", "audio.clockRate", config.audio.clockRate); !result) return result;
    if (const auto result = readInt(*section, "channelCount", "audio.channelCount", config.audio.channelCount); !result) return result;
    if (const auto result = readInt(*section, "ptimeMs", "audio.ptimeMs", config.audio.ptimeMs); !result) return result;
    if (const auto result = readInt(*section, "ecTailMs", "audio.ecTailMs", config.audio.ecTailMs); !result) return result;
    if (const auto result = readInt(*section, "quality", "audio.quality", config.audio.quality); !result) return result;
    return readBool(*section, "noVad", "audio.noVad", config.audio.noVad);
}

util::Result<void> parseCodecs(const Json& root, AppConfig& config, Warnings* warnings)
{
    const auto sectionResult = optionalObject(root, "codecs");
    if (!sectionResult) return util::Result<void>::failure(sectionResult.error());
    const Json* section = sectionResult.value();
    if (section == nullptr) return util::Result<void>::success();
    warnUnknownFields(*section, {"priority"}, "codecs", warnings);

    const auto priorities = section->find("priority");
    if (priorities == section->end()) return util::Result<void>::success();
    if (!priorities->is_object()) {
        return parseFailure("codecs.priority", "um objeto JSON");
    }
    for (auto codec = priorities->cbegin(); codec != priorities->cend(); ++codec) {
        int priority = 0;
        const Json wrapper = {{"value", codec.value()}};
        const std::string path = "codecs.priority." + codec.key();
        const auto result = readInt(wrapper, "value", path, priority);
        if (!result) return result;
        config.codecs.priority[codec.key()] = priority;
    }
    return util::Result<void>::success();
}

util::Result<void> parseDtmf(const Json& root, AppConfig& config, Warnings* warnings)
{
    const auto sectionResult = optionalObject(root, "dtmf");
    if (!sectionResult) return util::Result<void>::failure(sectionResult.error());
    const Json* section = sectionResult.value();
    if (section == nullptr) return util::Result<void>::success();
    warnUnknownFields(*section,
                      {"defaultMethod", "durationMs", "gapMs", "volumeDbm0", "localFeedback", "logDigits"},
                      "dtmf",
                      warnings);

    if (const auto result = readString(*section, "defaultMethod", "dtmf.defaultMethod", config.dtmf.defaultMethod); !result) return result;
    if (const auto result = readInt(*section, "durationMs", "dtmf.durationMs", config.dtmf.durationMs); !result) return result;
    if (const auto result = readInt(*section, "gapMs", "dtmf.gapMs", config.dtmf.gapMs); !result) return result;
    if (const auto result = readInt(*section, "volumeDbm0", "dtmf.volumeDbm0", config.dtmf.volumeDbm0); !result) return result;
    if (const auto result = readBool(*section, "localFeedback", "dtmf.localFeedback", config.dtmf.localFeedback); !result) return result;
    return readBool(*section, "logDigits", "dtmf.logDigits", config.dtmf.logDigits);
}

util::Result<void> parseLogging(const Json& root, AppConfig& config, Warnings* warnings)
{
    const auto sectionResult = optionalObject(root, "logging");
    if (!sectionResult) return util::Result<void>::failure(sectionResult.error());
    const Json* section = sectionResult.value();
    if (section == nullptr) return util::Result<void>::success();
    warnUnknownFields(*section,
                      {"consoleLevel", "fileLevel", "directory", "maxFileMB", "sipMessageTrace"},
                      "logging",
                      warnings);

    if (const auto result = readInt(*section, "consoleLevel", "logging.consoleLevel", config.logging.consoleLevel); !result) return result;
    if (const auto result = readInt(*section, "fileLevel", "logging.fileLevel", config.logging.fileLevel); !result) return result;
    if (const auto result = readString(*section, "directory", "logging.directory", config.logging.directory); !result) return result;
    if (const auto result = readInt(*section, "maxFileMB", "logging.maxFileMB", config.logging.maxFileMB); !result) return result;
    return readBool(*section, "sipMessageTrace", "logging.sipMessageTrace", config.logging.sipMessageTrace);
}

util::Result<void> parseBehavior(const Json& root, AppConfig& config, Warnings* warnings)
{
    const auto sectionResult = optionalObject(root, "behavior");
    if (!sectionResult) return util::Result<void>::failure(sectionResult.error());
    const Json* section = sectionResult.value();
    if (section == nullptr) return util::Result<void>::success();
    warnUnknownFields(
        *section, {"ringtoneEnabled", "topmostOnIncomingCall"}, "behavior", warnings);
    if (const auto result = readBool(*section, "ringtoneEnabled",
                                     "behavior.ringtoneEnabled",
                                     config.behavior.ringtoneEnabled); !result) {
        return result;
    }
    return readBool(*section, "topmostOnIncomingCall",
                    "behavior.topmostOnIncomingCall",
                    config.behavior.topmostOnIncomingCall);
}

} // namespace

util::Result<AppConfig> ConfigLoader::load(const std::filesystem::path& path, Warnings* warnings)
{
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input.is_open()) {
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            return util::Result<AppConfig>::failure(
                util::ErrorCode::NotFound,
                "Arquivo de configuração não encontrado; copie config/polphone.config.example.json para config/polphone.config.json.",
                path.u8string());
        }
        return util::Result<AppConfig>::failure(
            util::ErrorCode::Io,
            "Não foi possível abrir a configuração; verifique o caminho e as permissões de leitura.",
            path.u8string());
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) {
        return util::Result<AppConfig>::failure(
            util::ErrorCode::Io,
            "Falha ao ler a configuração; verifique o arquivo e tente novamente.",
            path.u8string());
    }
    return parse(contents.str(), warnings);
}

util::Result<AppConfig> ConfigLoader::parse(std::string_view document, Warnings* warnings)
{
    if (warnings != nullptr) warnings->clear();

    Json root;
    try {
        root = Json::parse(document.begin(), document.end());
    } catch (const Json::parse_error& error) {
        return util::Result<AppConfig>::failure(
            util::ErrorCode::Parse,
            "JSON malformado; corrija a sintaxe indicada pelo parser.",
            "JSON (byte " + std::to_string(error.byte) + ")");
    } catch (const Json::exception&) {
        return util::Result<AppConfig>::failure(
            util::ErrorCode::Parse,
            "Não foi possível interpretar o JSON; revise o conteúdo do arquivo.",
            "JSON");
    }

    if (!root.is_object()) {
        return util::Result<AppConfig>::failure(
            util::ErrorCode::Parse,
            "A raiz da configuração deve ser um objeto JSON; envolva os campos com chaves.",
            "$" );
    }

    warnUnknownFields(root, {"$schema", "sip", "network", "audio", "codecs", "dtmf", "logging", "behavior"}, "", warnings);

    AppConfig config;
    if (const auto result = parseSip(root, config, warnings); !result) {
        return util::Result<AppConfig>::failure(result.error());
    }
    if (const auto result = parseNetwork(root, config, warnings); !result) {
        return util::Result<AppConfig>::failure(result.error());
    }
    if (const auto result = parseAudio(root, config, warnings); !result) {
        return util::Result<AppConfig>::failure(result.error());
    }
    if (const auto result = parseCodecs(root, config, warnings); !result) {
        return util::Result<AppConfig>::failure(result.error());
    }
    if (const auto result = parseDtmf(root, config, warnings); !result) {
        return util::Result<AppConfig>::failure(result.error());
    }
    if (const auto result = parseLogging(root, config, warnings); !result) {
        return util::Result<AppConfig>::failure(result.error());
    }
    if (const auto result = parseBehavior(root, config, warnings); !result) {
        return util::Result<AppConfig>::failure(result.error());
    }
    return util::Result<AppConfig>::success(std::move(config));
}

} // namespace polphone::config
