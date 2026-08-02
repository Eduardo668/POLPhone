/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "app/CommandParser.h"

#include "util/Strings.h"

#include <charconv>
#include <cctype>
#include <string>
#include <vector>

namespace polphone::app {
namespace {

util::Result<Command> invalid(std::string message, std::string detail = {})
{
    return util::Result<Command>::failure(
        util::ErrorCode::InvalidArgument, std::move(message), std::move(detail));
}

std::string lowerAscii(std::string_view value)
{
    std::string result(value);
    for (char& character : result) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return result;
}

std::vector<std::string> tokenize(std::string_view line)
{
    std::vector<std::string> tokens;
    std::size_t position = 0;
    while (position < line.size()) {
        while (position < line.size()
               && std::isspace(static_cast<unsigned char>(line[position])) != 0) {
            ++position;
        }
        const std::size_t begin = position;
        while (position < line.size()
               && std::isspace(static_cast<unsigned char>(line[position])) == 0) {
            ++position;
        }
        if (position > begin) tokens.emplace_back(line.substr(begin, position - begin));
    }
    return tokens;
}

std::optional<int> parseInteger(std::string_view text)
{
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

bool isNoArgumentVerb(std::string_view verb, CommandVerb& parsed)
{
    if (verb == "help") parsed = CommandVerb::Help;
    else if (verb == "status") parsed = CommandVerb::Status;
    else if (verb == "devices") parsed = CommandVerb::Devices;
    else if (verb == "answer") parsed = CommandVerb::Answer;
    else if (verb == "hangup") parsed = CommandVerb::Hangup;
    else if (verb == "codecs") parsed = CommandVerb::Codecs;
    else if (verb == "quit") parsed = CommandVerb::Quit;
    else return false;
    return true;
}

} // namespace

util::Result<Command> CommandParser::parse(std::string_view line)
{
    const auto tokens = tokenize(util::trim(line));
    if (tokens.empty()) return invalid("Informe um comando; use help para ver as opções.");

    const std::string verb = lowerAscii(tokens[0]);
    Command command;
    if (isNoArgumentVerb(verb, command.verb)) {
        if (tokens.size() != 1U) {
            return invalid("O comando não aceita argumentos adicionais.", tokens[0]);
        }
        return util::Result<Command>::success(std::move(command));
    }

    if (verb == "setdev") {
        if (tokens.size() != 3U) return invalid("Uso: setdev in|out <id>.");
        const std::string direction = lowerAscii(tokens[1]);
        if (direction == "in") command.deviceDirection = DeviceDirection::Capture;
        else if (direction == "out") command.deviceDirection = DeviceDirection::Playback;
        else return invalid("A direção deve ser in ou out.", tokens[1]);
        const auto id = parseInteger(tokens[2]);
        if (!id.has_value() || *id < 0) return invalid("O id do dispositivo deve ser inteiro não negativo.", tokens[2]);
        command.verb = CommandVerb::SetDevice;
        command.value = *id;
        return util::Result<Command>::success(std::move(command));
    }

    if (verb == "reg") {
        if (tokens.size() != 2U) return invalid("Uso: reg on|off.");
        const std::string state = lowerAscii(tokens[1]);
        if (state == "on") command.enabled = true;
        else if (state == "off") command.enabled = false;
        else return invalid("O estado do registro deve ser on ou off.", tokens[1]);
        command.verb = CommandVerb::Registration;
        return util::Result<Command>::success(std::move(command));
    }

    if (verb == "call") {
        if (tokens.size() != 2U) return invalid("Uso: call <destino>.");
        command.verb = CommandVerb::Call;
        command.text = tokens[1];
        return util::Result<Command>::success(std::move(command));
    }

    if (verb == "dtmf") {
        if (tokens.size() < 2U) {
            return invalid("Uso: dtmf <dígitos> [--method método] [--duration ms] [--gap ms].");
        }
        command.verb = CommandVerb::Dtmf;
        command.text = tokens[1];
        for (std::size_t index = 2U; index < tokens.size(); index += 2U) {
            if (index + 1U >= tokens.size()) return invalid("Uma flag DTMF está sem valor.", tokens[index]);
            const std::string flag = lowerAscii(tokens[index]);
            if (flag == "--method") {
                if (command.method.has_value()) return invalid("--method foi informado mais de uma vez.");
                command.method = dtmf::parseMethod(tokens[index + 1U]);
                if (!command.method.has_value()) {
                    return invalid(
                        "Método DTMF inválido; use rfc4733, inband ou info.",
                        tokens[index + 1U]);
                }
            } else if (flag == "--duration") {
                if (command.durationMs.has_value()) return invalid("--duration foi informado mais de uma vez.");
                command.durationMs = parseInteger(tokens[index + 1U]);
                if (!command.durationMs.has_value() || *command.durationMs <= 0) {
                    return invalid(
                        "A duração deve ser um inteiro positivo.", tokens[index + 1U]);
                }
            } else if (flag == "--gap") {
                if (command.gapMs.has_value()) return invalid("--gap foi informado mais de uma vez.");
                command.gapMs = parseInteger(tokens[index + 1U]);
                if (!command.gapMs.has_value() || *command.gapMs <= 0) {
                    return invalid(
                        "O intervalo deve ser um inteiro positivo.", tokens[index + 1U]);
                }
            } else {
                return invalid("Flag DTMF desconhecida.", tokens[index]);
            }
        }
        return util::Result<Command>::success(std::move(command));
    }

    if (verb == "dtmfmode") {
        if (tokens.size() != 2U) return invalid("Uso: dtmfmode rfc4733|inband|info.");
        command.method = dtmf::parseMethod(tokens[1]);
        if (!command.method.has_value()) {
            return invalid(
                "Método DTMF inválido; use rfc4733, inband ou info.", tokens[1]);
        }
        command.verb = CommandVerb::DtmfMode;
        return util::Result<Command>::success(std::move(command));
    }

    if (verb == "dtmfcfg") {
        if (tokens.size() != 3U) return invalid("Uso: dtmfcfg duration|gap|volume <valor>.");
        const std::string field = lowerAscii(tokens[1]);
        if (field == "duration") command.dtmfField = DtmfConfigField::Duration;
        else if (field == "gap") command.dtmfField = DtmfConfigField::Gap;
        else if (field == "volume") command.dtmfField = DtmfConfigField::Volume;
        else return invalid("Campo DTMF inválido; use duration, gap ou volume.", tokens[1]);
        command.value = parseInteger(tokens[2]);
        if (!command.value.has_value()) return invalid("O valor DTMF deve ser um inteiro.", tokens[2]);
        command.verb = CommandVerb::DtmfConfig;
        return util::Result<Command>::success(std::move(command));
    }

    if (verb == "loglevel") {
        if (tokens.size() != 2U) return invalid("Uso: loglevel <0..6>.");
        command.value = parseInteger(tokens[1]);
        if (!command.value.has_value() || *command.value < 0 || *command.value > 6) {
            return invalid("O nível de log deve estar entre 0 e 6.", tokens[1]);
        }
        command.verb = CommandVerb::LogLevel;
        return util::Result<Command>::success(std::move(command));
    }

    return invalid("Comando desconhecido; use help para ver as opções.", tokens[0]);
}

} // namespace polphone::app
