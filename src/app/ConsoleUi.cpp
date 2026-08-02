/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "app/ConsoleUi.h"

#include "app/Application.h"

#include <Windows.h>

#include <atomic>
#include <iostream>
#include <string>

namespace polphone::app {
namespace {

std::atomic<std::atomic<bool>*> activeInterruptFlag{nullptr};
std::atomic<HANDLE> consoleThread{nullptr};

BOOL WINAPI handleConsoleControl(DWORD controlType)
{
    if (controlType != CTRL_C_EVENT && controlType != CTRL_BREAK_EVENT
        && controlType != CTRL_CLOSE_EVENT) {
        return FALSE;
    }
    if (std::atomic<bool>* flag = activeInterruptFlag.load(); flag != nullptr) {
        flag->store(true);
    }
    if (HANDLE thread = consoleThread.load(); thread != nullptr) {
        static_cast<void>(CancelSynchronousIo(thread));
    }
    return TRUE;
}

std::string_view registrationPrompt(RegistrationState state) noexcept
{
    switch (state) {
    case RegistrationState::Registered: return "OK";
    case RegistrationState::Registering: return "WAIT";
    case RegistrationState::Failed: return "FAIL";
    case RegistrationState::Unregistering: return "STOP";
    case RegistrationState::Unregistered: return "OFF";
    case RegistrationState::Unconfigured: return "N/A";
    }
    return "?";
}

std::string_view severityName(UiEventSeverity severity) noexcept
{
    switch (severity) {
    case UiEventSeverity::Info: return "INFO";
    case UiEventSeverity::Warning: return "WARN";
    case UiEventSeverity::Error: return "ERROR";
    }
    return "?";
}

} // namespace

ConsoleUi::ConsoleUi(
    Application& application,
    std::istream& input,
    std::ostream& output,
    std::ostream& error) noexcept
    : application_(application), input_(input), output_(output), error_(error)
{
}

int ConsoleUi::run()
{
    HANDLE currentThread = nullptr;
    if (DuplicateHandle(
            GetCurrentProcess(),
            GetCurrentThread(),
            GetCurrentProcess(),
            &currentThread,
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS)
        == FALSE) {
        error_ << "Erro: não foi possível preparar o encerramento por Ctrl+C.\n";
        return 3;
    }
    consoleThread.store(currentThread);
    activeInterruptFlag.store(&interrupted_);
    if (SetConsoleCtrlHandler(handleConsoleControl, TRUE) == FALSE) {
        activeInterruptFlag.store(nullptr);
        consoleThread.store(nullptr);
        CloseHandle(currentThread);
        error_ << "Erro: não foi possível instalar o handler de Ctrl+C.\n";
        return 3;
    }

    const auto cleanupHandler = [&] {
        static_cast<void>(SetConsoleCtrlHandler(handleConsoleControl, FALSE));
        activeInterruptFlag.store(nullptr);
        consoleThread.store(nullptr);
        CloseHandle(currentThread);
    };

    output_ << "Digite help para ver os comandos disponíveis.\n";
    while (!interrupted_.load()) {
        static_cast<void>(application_.reapCalls());
        drainEvents();
        output_ << prompt();
        output_.flush();

        std::string line;
        if (!std::getline(input_, line)) {
            const bool interrupted = interrupted_.load();
            cleanupHandler();
            return interrupted ? 130 : 0;
        }
        const auto parsed = CommandParser::parse(line);
        if (!parsed) {
            printError(parsed.error());
            continue;
        }
        if (dispatch(parsed.value()) == DispatchResult::Quit) {
            cleanupHandler();
            return 0;
        }
    }
    cleanupHandler();
    return 130;
}

void ConsoleUi::printHelp()
{
    output_
        << "Comandos:\n"
        << "  help                                  lista comandos\n"
        << "  status                                mostra registro, chamada, áudio e DTMF\n"
        << "  devices                               lista dispositivos de áudio\n"
        << "  setdev in|out <id>                    seleciona dispositivo\n"
        << "  reg on|off                            ativa ou remove o registro\n"
        << "  call <destino>                        inicia chamada\n"
        << "  answer                                atende chamada entrante\n"
        << "  hangup                                encerra chamada\n"
        << "  dtmf <dígitos> [--method ...] [--duration ms] [--gap ms]\n"
        << "  dtmfmode rfc4733|inband|info          define método padrão\n"
        << "  dtmfcfg duration|gap|volume <valor>   ajusta parâmetros DTMF\n"
        << "  codecs                                lista codecs efetivos\n"
        << "  loglevel <0..6>                       ajusta o log do console\n"
        << "  quit                                  encerra com segurança\n";
}

void ConsoleUi::printStatus()
{
    const ApplicationStatus status = application_.status();
    output_ << "Registro: " << registrationStateName(status.registration.state)
            << " active=" << (status.registration.active ? "sim" : "não")
            << " code=" << status.registration.sipCode
            << " expires=" << status.registration.expiresSec << "s\n"
            << "Chamada: " << callStateName(status.call.state)
            << " code=" << status.call.sipCode
            << " remote=" << (status.call.remoteUri.empty() ? "-" : status.call.remoteUri)
            << " reason=" << (status.call.reason.empty() ? "-" : status.call.reason) << '\n'
            << "Áudio: capture=";
    if (status.captureDevice.has_value()) output_ << *status.captureDevice;
    else output_ << "padrão";
    output_ << " playback=";
    if (status.playbackDevice.has_value()) output_ << *status.playbackDevice;
    else output_ << "padrão";
    output_ << "\nDTMF: method=" << status.dtmfMethod
            << " duration=" << status.dtmf.durationMs << "ms"
            << " gap=" << status.dtmf.gapMs << "ms"
            << " volume=" << status.dtmf.volumeDbm0 << "dBm0\n"
            << "Log do console: " << status.consoleLogLevel << '\n';
}

void ConsoleUi::printDevices()
{
    const auto devices = application_.listAudioDevices();
    if (!devices) {
        printError(devices.error());
        return;
    }
    output_ << "Dispositivos de áudio:\n";
    for (const auto& device : devices.value()) {
        output_ << "  #" << device.id << " [in:" << device.inputCount
                << "][out:" << device.outputCount << "] " << device.name
                << " — " << device.driver << " @ "
                << device.defaultSamplesPerSec << " Hz\n";
    }
}

void ConsoleUi::printCodecs()
{
    const auto codecs = application_.listCodecs();
    if (!codecs) {
        printError(codecs.error());
        return;
    }
    output_ << "Codecs efetivos:\n";
    for (const auto& codec : codecs.value()) {
        output_ << "  " << codec.id << " priority=" << codec.priority;
        if (!codec.description.empty()) output_ << " — " << codec.description;
        output_ << '\n';
    }
}

void ConsoleUi::drainEvents()
{
    for (const auto& event : application_.drainEvents()) {
        output_ << '[' << severityName(event.severity) << "][" << event.category
                << "] " << event.text << '\n';
    }
}

void ConsoleUi::printError(const util::Error& error)
{
    error_ << "Erro: " << error.message;
    if (!error.detail.empty()) error_ << " (" << error.detail << ')';
    error_ << '\n';
}

std::string ConsoleUi::prompt() const
{
    const ApplicationStatus status = application_.status();
    return "POLPhone [reg:" + std::string(registrationPrompt(status.registration.state))
        + "][call:" + std::string(callStateName(status.call.state))
        + "][dtmf:" + status.dtmfMethod + "]> ";
}

ConsoleUi::DispatchResult ConsoleUi::dispatch(const Command& command)
{
    util::Result<void> result = util::Result<void>::success();
    switch (command.verb) {
    case CommandVerb::Help:
        printHelp();
        break;
    case CommandVerb::Status:
        printStatus();
        break;
    case CommandVerb::Devices:
        printDevices();
        break;
    case CommandVerb::SetDevice:
        result = application_.selectAudioDevice(
            command.deviceDirection == DeviceDirection::Capture
                ? audio::AudioDeviceDirection::Capture
                : audio::AudioDeviceDirection::Playback,
            *command.value);
        if (result) output_ << "Dispositivo selecionado.\n";
        break;
    case CommandVerb::Registration:
        result = application_.setRegistrationEnabled(*command.enabled);
        if (result) output_ << "Solicitação de registro enviada.\n";
        break;
    case CommandVerb::Call:
        result = application_.makeCall(command.text);
        if (result) output_ << "Chamada iniciada.\n";
        break;
    case CommandVerb::Answer:
        result = application_.answerCall();
        if (result) output_ << "Atendimento solicitado.\n";
        break;
    case CommandVerb::Hangup:
        result = application_.hangupCall();
        if (result) output_ << "Encerramento solicitado.\n";
        break;
    case CommandVerb::Dtmf: {
        const auto sent = application_.sendDtmf(
            command.text, command.method, command.durationMs, command.gapMs);
        if (!sent) printError(sent.error());
        else output_ << sent.value().summary << '\n';
        break;
    }
    case CommandVerb::DtmfMode:
        result = application_.setDtmfDefaultMethod(*command.method);
        if (result) {
            output_ << "Método DTMF padrão atualizado para "
                    << dtmf::methodName(*command.method) << ".\n";
        }
        break;
    case CommandVerb::DtmfConfig:
        switch (*command.dtmfField) {
        case DtmfConfigField::Duration:
            result = application_.setDtmfDurationMs(*command.value);
            break;
        case DtmfConfigField::Gap:
            result = application_.setDtmfGapMs(*command.value);
            break;
        case DtmfConfigField::Volume:
            result = application_.setDtmfVolumeDbm0(*command.value);
            break;
        }
        if (result) output_ << "Configuração DTMF atualizada.\n";
        break;
    case CommandVerb::Codecs:
        printCodecs();
        break;
    case CommandVerb::LogLevel:
        result = application_.setConsoleLogLevel(*command.value);
        if (result) output_ << "Nível de log do console atualizado.\n";
        break;
    case CommandVerb::Quit:
        return DispatchResult::Quit;
    }
    if (!result) printError(result.error());
    return DispatchResult::Continue;
}

} // namespace polphone::app
