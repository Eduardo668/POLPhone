/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "config/ConfigValidator.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace polphone::config {
namespace {

util::Result<void> invalid(std::string path, std::string requirement)
{
    return util::Result<void>::failure(
        util::ErrorCode::Validation,
        "Valor inválido; " + std::move(requirement) + ".",
        std::move(path));
}

bool containsWhitespace(std::string_view value)
{
    return std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
}

bool iequalsAscii(std::string_view left, std::string_view right)
{
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto leftCharacter = static_cast<unsigned char>(left[index]);
        const auto rightCharacter = static_cast<unsigned char>(right[index]);
        if (std::tolower(leftCharacter) != std::tolower(rightCharacter)) return false;
    }
    return true;
}

bool isSipUri(std::string_view value)
{
    if (value.size() <= 4U || containsWhitespace(value)) return false;
    if (!((value[0] == 's' || value[0] == 'S')
        && (value[1] == 'i' || value[1] == 'I')
        && (value[2] == 'p' || value[2] == 'P')
        && value[3] == ':')) {
        return false;
    }
    const std::size_t at = value.find('@', 4U);
    return at == std::string_view::npos
        || (at > 4U && at + 1U < value.size());
}

util::Result<void> requireNotEmpty(std::string_view value,
                                   std::string path,
                                   std::string instruction)
{
    if (value.empty()) return invalid(std::move(path), std::move(instruction));
    return util::Result<void>::success();
}

util::Result<void> requireRange(int value, int minimum, int maximum, std::string path)
{
    if (value < minimum || value > maximum) {
        return invalid(
            std::move(path),
            "use um inteiro entre " + std::to_string(minimum) + " e " + std::to_string(maximum));
    }
    return util::Result<void>::success();
}

} // namespace

util::Result<void> ConfigValidator::validate(const AppConfig& config)
{
    if (!isSipUri(config.sip.idUri)
        || config.sip.idUri.find('@', 4U) == std::string::npos) {
        return invalid("sip.idUri", "use uma URI no formato sip:usuario@dominio");
    }
    if (!isSipUri(config.sip.registrarUri)) {
        return invalid("sip.registrarUri", "use uma URI iniciada por sip:");
    }
    if (!config.sip.proxyUri.empty() && !isSipUri(config.sip.proxyUri)) {
        return invalid("sip.proxyUri", "deixe vazio ou use uma URI iniciada por sip:");
    }
    if (const auto result = requireNotEmpty(config.sip.realm, "sip.realm", "informe o realm ou use *"); !result) return result;
    if (const auto result = requireNotEmpty(config.sip.username, "sip.username", "informe o usuário SIP"); !result) return result;
    if (containsWhitespace(config.sip.authUsername)) {
        return invalid("sip.authUsername", "não use espaços no usuário de autenticação");
    }
    if (config.sip.displayName.find('\r') != std::string::npos
        || config.sip.displayName.find('\n') != std::string::npos) {
        return invalid("sip.displayName", "não use quebras de linha no nome de exibição");
    }
    if (const auto result = requireNotEmpty(config.sip.password, "sip.password", "informe a senha SIP"); !result) return result;
    if (const auto result = requireNotEmpty(config.sip.domain, "sip.domain", "informe o domínio SIP"); !result) return result;
    if (config.sip.regTimeoutSec <= 0) return invalid("sip.regTimeoutSec", "use um valor maior que zero");
    if (config.sip.regRetryIntervalSec < 0) return invalid("sip.regRetryIntervalSec", "use zero ou um valor positivo");

    if (const auto result = requireRange(config.network.localPort, 0, 65535, "network.localPort"); !result) return result;
    if (!iequalsAscii(config.network.transport, "udp")) {
        return invalid("network.transport", "use udp, o único transporte disponível no MVP");
    }

    if (config.audio.clockRate <= 0) return invalid("audio.clockRate", "use uma taxa positiva em hertz");
    if (config.audio.channelCount != 1) return invalid("audio.channelCount", "use 1 canal no MVP");
    if (config.audio.ptimeMs <= 0) return invalid("audio.ptimeMs", "use um intervalo positivo em milissegundos");
    if (config.audio.ecTailMs < 0) return invalid("audio.ecTailMs", "use zero ou um valor positivo");
    if (const auto result = requireRange(config.audio.quality, 8, 10, "audio.quality"); !result) return result;

    for (const auto& codec : config.codecs.priority) {
        if (codec.first.empty()) return invalid("codecs.priority", "informe um identificador de codec não vazio");
        if (const auto result = requireRange(codec.second, 0, 255, "codecs.priority." + codec.first); !result) return result;
    }

    if (!iequalsAscii(config.dtmf.defaultMethod, "rfc4733")
        && !iequalsAscii(config.dtmf.defaultMethod, "inband")
        && !iequalsAscii(config.dtmf.defaultMethod, "info")) {
        return invalid("dtmf.defaultMethod", "use rfc4733, inband ou info");
    }
    if (const auto result = requireRange(config.dtmf.durationMs, 40, 2000, "dtmf.durationMs"); !result) return result;
    if (const auto result = requireRange(config.dtmf.gapMs, 20, 2000, "dtmf.gapMs"); !result) return result;
    if (const auto result = requireRange(config.dtmf.volumeDbm0, -30, 0, "dtmf.volumeDbm0"); !result) return result;

    if (const auto result = requireRange(config.logging.consoleLevel, 0, 6, "logging.consoleLevel"); !result) return result;
    if (const auto result = requireRange(config.logging.fileLevel, 0, 6, "logging.fileLevel"); !result) return result;
    if (const auto result = requireNotEmpty(config.logging.directory, "logging.directory", "informe um diretório de logs"); !result) return result;
    if (config.logging.maxFileMB <= 0) return invalid("logging.maxFileMB", "use um tamanho maior que zero");

    return util::Result<void>::success();
}

} // namespace polphone::config
