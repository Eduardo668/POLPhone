/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "config/AppConfig.h"

#include <nlohmann/json.hpp>

#include <string_view>

namespace polphone::config {
namespace {

std::string redactSecret(std::string value, std::string_view secret)
{
    if (secret.empty()) return value;
    std::size_t position = 0;
    while ((position = value.find(secret, position)) != std::string::npos) {
        value.replace(position, secret.size(), "***");
        position += 3U;
    }
    return value;
}

} // namespace

std::string AppConfig::redactedDump() const
{
    std::map<std::string, int> safePriorities;
    for (const auto& priority : codecs.priority) {
        safePriorities.emplace(redactSecret(priority.first, sip.password), priority.second);
    }

    const nlohmann::json document = {
        {"sip",
         {{"idUri", redactSecret(sip.idUri, sip.password)},
          {"displayName", redactSecret(sip.displayName, sip.password)},
          {"registrarUri", redactSecret(sip.registrarUri, sip.password)},
          {"realm", redactSecret(sip.realm, sip.password)},
          {"username", redactSecret(sip.username, sip.password)},
          {"authUsername", redactSecret(sip.authUsername, sip.password)},
          {"password", "***"},
          {"domain", redactSecret(sip.domain, sip.password)},
          {"proxyUri", redactSecret(sip.proxyUri, sip.password)},
          {"regTimeoutSec", sip.regTimeoutSec},
          {"regRetryIntervalSec", sip.regRetryIntervalSec},
          {"registerOnStartup", sip.registerOnStartup}}},
        {"network",
         {{"localPort", network.localPort},
          {"boundAddress", redactSecret(network.boundAddress, sip.password)},
          {"transport", redactSecret(network.transport, sip.password)}}},
        {"audio",
         {{"captureDevice", redactSecret(audio.captureDevice, sip.password)},
          {"playbackDevice", redactSecret(audio.playbackDevice, sip.password)},
          {"clockRate", audio.clockRate},
          {"channelCount", audio.channelCount},
          {"ptimeMs", audio.ptimeMs},
          {"ecTailMs", audio.ecTailMs},
          {"quality", audio.quality},
          {"noVad", audio.noVad}}},
        {"codecs", {{"priority", safePriorities}}},
        {"dtmf",
         {{"defaultMethod", redactSecret(dtmf.defaultMethod, sip.password)},
          {"durationMs", dtmf.durationMs},
          {"gapMs", dtmf.gapMs},
          {"volumeDbm0", dtmf.volumeDbm0},
          {"localFeedback", dtmf.localFeedback},
          {"logDigits", dtmf.logDigits}}},
        {"logging",
         {{"consoleLevel", logging.consoleLevel},
          {"fileLevel", logging.fileLevel},
          {"directory", redactSecret(logging.directory, sip.password)},
          {"maxFileMB", logging.maxFileMB},
          {"sipMessageTrace", logging.sipMessageTrace}}},
        {"behavior",
         {{"ringtoneEnabled", behavior.ringtoneEnabled},
          {"topmostOnIncomingCall", behavior.topmostOnIncomingCall}}},
    };
    return document.dump();
}

} // namespace polphone::config
