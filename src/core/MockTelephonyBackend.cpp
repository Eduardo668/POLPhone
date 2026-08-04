/* POLPhone - backend de demonstração sem PJSIP e sem rede. GPL-2.0-only. */

#include "core/MockTelephonyBackend.h"

#include "core/Presentation.h"
#include "dtmf/DtmfPlan.h"

#include <algorithm>
#include <utility>

namespace polphone::core {

MockTelephonyBackend::MockTelephonyBackend()
{
    state_.demoMode = true;
    state_.endpointState = "Parado (demonstração)";
    state_.devices = {
        {0, "Microfone de demonstração", true, false},
        {1, "Headset POL (simulado)", true, true},
        {2, "Alto-falantes de demonstração", false, true},
    };
    state_.captureDevice = 1;
    state_.playbackDevice = 1;
}

util::Result<void> MockTelephonyBackend::requireReady() const
{
    if (state_.application != ApplicationState::Ready) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "O telefone ainda não está pronto.");
    }
    return util::Result<void>::success();
}

void MockTelephonyBackend::change(std::string logLine)
{
    ++state_.revision;
    if (!logLine.empty()) {
        state_.sanitizedLogs.push_back(std::move(logLine));
        constexpr std::size_t maximumLines = 80U;
        if (state_.sanitizedLogs.size() > maximumLines) {
            state_.sanitizedLogs.erase(
                state_.sanitizedLogs.begin(),
                state_.sanitizedLogs.begin()
                    + static_cast<std::ptrdiff_t>(state_.sanitizedLogs.size() - maximumLines));
        }
    }
}

void MockTelephonyBackend::schedule(std::uint64_t delayMs, std::function<void()> action)
{
    scheduled_.push_back(ScheduledAction{nowMs_ + delayMs, std::move(action)});
}

util::Result<void> MockTelephonyBackend::initialize()
{
    if (state_.application != ApplicationState::Stopped) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "A demonstração já foi inicializada.");
    }
    state_.application = ApplicationState::Initializing;
    state_.endpointState = "Inicializando backend simulado";
    state_.friendlyError.clear();
    state_.technicalDetail.clear();
    change("[demo] Aplicativo inicializando; nenhum servidor SIP foi acessado.");
    schedule(250U, [this] {
        state_.application = ApplicationState::Ready;
        state_.registration = RegistrationState::Disconnected;
        state_.endpointState = "Pronto (backend simulado)";
        change("[demo] Aplicativo pronto e registro desconectado.");
    });
    return util::Result<void>::success();
}

util::Result<void> MockTelephonyBackend::shutdown()
{
    if (state_.application == ApplicationState::Stopped) {
        return util::Result<void>::success();
    }
    scheduled_.clear();
    state_.application = ApplicationState::ShuttingDown;
    state_.registration = RegistrationState::Disconnected;
    clearCall();
    change("[demo] Encerramento ordenado iniciado.");
    schedule(200U, [this] {
        state_.application = ApplicationState::Stopped;
        state_.endpointState = "Parado (demonstração)";
        change("[demo] Encerramento ordenado concluído.");
    });
    return util::Result<void>::success();
}

util::Result<void> MockTelephonyBackend::registerAccount()
{
    if (auto ready = requireReady(); !ready) return ready;
    if (state_.registration == RegistrationState::Connecting
        || state_.registration == RegistrationState::Connected) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "O registro já está ativo ou em andamento.");
    }
    state_.registration = RegistrationState::Connecting;
    state_.friendlyError.clear();
    change("[demo] Registro conectando.");
    schedule(650U, [this] {
        state_.registration = RegistrationState::Connected;
        change("[demo] Registro ativo.");
    });
    return util::Result<void>::success();
}

util::Result<void> MockTelephonyBackend::unregisterAccount()
{
    if (auto ready = requireReady(); !ready) return ready;
    if (state_.registration != RegistrationState::Connected) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "Não existe registro ativo para remover.");
    }
    if (state_.call != CallState::Idle && state_.call != CallState::Disconnected) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "Encerre a chamada antes de remover o registro.");
    }
    state_.registration = RegistrationState::Disconnecting;
    change("[demo] Remoção do registro solicitada.");
    schedule(350U, [this] {
        state_.registration = RegistrationState::Disconnected;
        change("[demo] Registro desconectado.");
    });
    return util::Result<void>::success();
}

util::Result<void> MockTelephonyBackend::makeCall(std::string_view destination)
{
    if (auto ready = requireReady(); !ready) return ready;
    if (state_.registration != RegistrationState::Connected) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "Conecte o registro antes de iniciar a chamada.");
    }
    if (state_.call != CallState::Idle && state_.call != CallState::Disconnected
        && state_.call != CallState::Error) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "O POLPhone suporta somente uma chamada ativa.");
    }
    auto validated = validateDestination(destination);
    if (!validated) return util::Result<void>::failure(validated.error());
    state_.maskedRemote = maskDestination(validated.value());
    state_.remoteNumber = validated.value();
    state_.callDirection = CallDirection::Outgoing;
    state_.call = CallState::OutgoingDialing;
    state_.media = MediaState::Inactive;
    state_.friendlyError.clear();
    state_.technicalDetail.clear();
    state_.muted = false;
    change("[demo] Chamada de saída em estado Chamando.");
    schedule(450U, [this] {
        state_.call = CallState::OutgoingRinging;
        change("[demo] Destino tocando.");
    });
    schedule(1050U, [this] {
        state_.call = CallState::Active;
        state_.media = MediaState::Active;
        state_.codec = "PCMU/8000/1 (simulado)";
        confirmedAtMs_ = nowMs_;
        change("[demo] Chamada confirmada; áudio simulado ativo.");
    });
    return util::Result<void>::success();
}

util::Result<void> MockTelephonyBackend::answerCall()
{
    if (state_.call != CallState::IncomingRinging
        || state_.callDirection != CallDirection::Incoming) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "Não existe chamada recebida para atender.");
    }
    state_.call = CallState::Connecting;
    change("[demo] Atendimento solicitado.");
    schedule(450U, [this] {
        state_.call = CallState::Active;
        state_.media = MediaState::Active;
        state_.codec = "PCMA/8000/1 (simulado)";
        confirmedAtMs_ = nowMs_;
        change("[demo] Chamada recebida atendida; áudio simulado ativo.");
    });
    return util::Result<void>::success();
}

util::Result<void> MockTelephonyBackend::rejectCall()
{
    if (state_.call != CallState::IncomingRinging
        || state_.callDirection != CallDirection::Incoming) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "Não existe chamada recebida para rejeitar.");
    }
    state_.call = CallState::Disconnecting;
    change("[demo] Chamada recebida rejeitada com 486 Busy Here simulado.");
    schedule(300U, [this] { clearCall(); change("[demo] Chamada rejeitada e encerrada."); });
    return util::Result<void>::success();
}

util::Result<void> MockTelephonyBackend::hangupCall()
{
    if (state_.call == CallState::Idle || state_.call == CallState::Disconnected
        || state_.call == CallState::Disconnecting) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "Não existe chamada ativa para encerrar.");
    }
    state_.call = CallState::Disconnecting;
    state_.dtmfInFlight = false;
    change("[demo] Encerramento da chamada solicitado.");
    schedule(350U, [this] { clearCall(); change("[demo] Chamada encerrada."); });
    return util::Result<void>::success();
}

util::Result<void> MockTelephonyBackend::applyDtmfSettings(
    const DtmfRuntimeSettings& settings)
{
    if (settings.method == DtmfMethod::Automatic) {
        return util::Result<void>::failure(
            util::ErrorCode::InvalidArgument,
            "Escolha um método DTMF padrão explícito.");
    }
    if (settings.durationMs < 40U || settings.durationMs > 2000U
        || settings.gapMs < 20U || settings.gapMs > 2000U
        || settings.inbandVolumeDbm0 < -30 || settings.inbandVolumeDbm0 > 0) {
        return util::Result<void>::failure(
            util::ErrorCode::InvalidArgument,
            "A configuração DTMF dinâmica é inválida.");
    }
    const bool changed = state_.dtmfConfiguredMethod != settings.method
        || state_.dtmfEffectiveMethod != settings.method
        || state_.dtmfDurationMs != settings.durationMs
        || state_.dtmfGapMs != settings.gapMs
        || state_.inbandVolumeDbm0 != settings.inbandVolumeDbm0;
    if (!changed) return util::Result<void>::success();
    const auto oldMethod = state_.dtmfEffectiveMethod;
    state_.dtmfConfiguredMethod = settings.method;
    state_.dtmfEffectiveMethod = settings.method;
    state_.dtmfDurationMs = settings.durationMs;
    state_.dtmfGapMs = settings.gapMs;
    state_.inbandVolumeDbm0 = settings.inbandVolumeDbm0;
    change("[demo] DTMF configuration changed: oldMethod="
        + std::string(dtmfMethodText(oldMethod)) + " newMethod="
        + std::string(dtmfMethodText(settings.method))
        + " appliedAtRuntime=true");
    return util::Result<void>::success();
}

util::Result<void> MockTelephonyBackend::sendDtmf(
    std::string_view digits,
    DtmfMethod method)
{
    if (state_.call != CallState::Active || state_.media != MediaState::Active) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "DTMF só está disponível durante uma chamada com áudio ativo.");
    }
    if (state_.dtmfInFlight) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "Já existe um envio DTMF em andamento.");
    }
    auto plan = dtmf::DtmfPlan::build(
        digits, state_.dtmfDurationMs, state_.dtmfGapMs);
    if (!plan) return util::Result<void>::failure(plan.error());
    const DtmfMethod effective = method == DtmfMethod::Automatic
        ? state_.dtmfEffectiveMethod : method;
    state_.lastDtmfMethod = effective;
    state_.lastDtmfResult = "Em andamento";
    state_.dtmfInFlight = true;
    change("[demo] DTMF simulado: configuredMethod="
        + std::string(dtmfMethodText(state_.dtmfConfiguredMethod))
        + " effectiveMethod=" + std::string(dtmfMethodText(effective))
        + " digit=[REDACTED] status=IN_PROGRESS.");
    std::uint64_t total = 0U;
    for (const auto& step : plan.value()) {
        total += step.kind == dtmf::DtmfPlanStep::Kind::Pause
            ? step.pauseMs : step.onMs + step.offMs;
    }
    schedule((std::max)(total, std::uint64_t{100}), [this] {
        state_.dtmfInFlight = false;
        state_.lastDtmfResult = "OK";
        change("[demo] Envio DTMF simulado concluído.");
    });
    return util::Result<void>::success();
}

util::Result<void> MockTelephonyBackend::setMuted(bool muted)
{
    if (state_.call != CallState::Active) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "O mudo só pode ser alterado durante uma chamada.");
    }
    state_.muted = muted;
    change(muted ? "[demo] Microfone silenciado." : "[demo] Microfone reativado.");
    return util::Result<void>::success();
}

util::Result<std::vector<AudioDevice>> MockTelephonyBackend::listAudioDevices()
{
    return util::Result<std::vector<AudioDevice>>::success(state_.devices);
}

util::Result<void> MockTelephonyBackend::selectAudioDevice(
    AudioDirection direction,
    int id)
{
    const auto found = std::find_if(
        state_.devices.begin(), state_.devices.end(), [=](const AudioDevice& device) {
            return device.id == id
                && (direction == AudioDirection::Capture ? device.capture : device.playback);
        });
    if (found == state_.devices.end()) {
        return util::Result<void>::failure(
            util::ErrorCode::NotFound, "O dispositivo de áudio selecionado não está disponível.");
    }
    if (direction == AudioDirection::Capture) state_.captureDevice = id;
    else state_.playbackDevice = id;
    change("[demo] Dispositivo de áudio simulado selecionado: " + found->name + ".");
    return util::Result<void>::success();
}

util::Result<void> MockTelephonyBackend::simulate(DemoScenario scenario)
{
    if (auto ready = requireReady(); !ready) return ready;
    scheduled_.clear();
    switch (scenario) {
    case DemoScenario::IncomingCall:
        if (state_.registration != RegistrationState::Connected || state_.call != CallState::Idle) {
            return util::Result<void>::failure(
                util::ErrorCode::Runtime,
                "A chamada recebida simulada exige registro conectado e nenhuma chamada ativa.");
        }
        state_.callDirection = CallDirection::Incoming;
        state_.call = CallState::IncomingRinging;
        state_.maskedRemote = "******4321";
        state_.remoteDisplayName = "Maria Demo";
        state_.remoteNumber = "4321";
        state_.friendlyError.clear();
        change("[demo] Chamada recebida simulada.");
        break;
    case DemoScenario::RegistrationFailure:
        clearCall();
        state_.registration = RegistrationState::Connecting;
        state_.friendlyError.clear();
        change("[demo] Simulando tentativa de registro.");
        schedule(500U, [this] {
            state_.registration = RegistrationState::Failed;
            state_.friendlyError = "Não foi possível conectar ao servidor SIP simulado.";
            state_.technicalDetail = "DEMO_REGISTRATION_TIMEOUT";
            change("[demo] Falha de registro simulada.");
        });
        break;
    case DemoScenario::CallFailure:
        if (state_.registration != RegistrationState::Connected || state_.call != CallState::Idle) {
            return util::Result<void>::failure(
                util::ErrorCode::Runtime,
                "A falha de chamada exige registro conectado e nenhuma chamada ativa.");
        }
        state_.callDirection = CallDirection::Outgoing;
        state_.call = CallState::OutgoingDialing;
        state_.maskedRemote = "****9999";
        state_.friendlyError.clear();
        change("[demo] Simulando chamada que falhará.");
        schedule(600U, [this] {
            state_.call = CallState::Error;
            state_.friendlyError = "A chamada simulada não pôde ser completada.";
            state_.technicalDetail = "486 Busy Here (simulado)";
            change("[demo] Falha de chamada simulada.");
        });
        break;
    case DemoScenario::ConnectionLoss:
        clearCall();
        state_.registration = RegistrationState::Failed;
        state_.friendlyError = "A conexão simulada foi perdida.";
        state_.technicalDetail = "DEMO_CONNECTION_LOST";
        change("[demo] Perda de conexão simulada.");
        break;
    }
    return util::Result<void>::success();
}

void MockTelephonyBackend::clearCall()
{
    state_.call = CallState::Idle;
    state_.callDirection = CallDirection::None;
    state_.media = MediaState::Inactive;
    state_.maskedRemote.clear();
    state_.remoteDisplayName.clear();
    state_.remoteNumber.clear();
    state_.codec.clear();
    state_.muted = false;
    state_.dtmfInFlight = false;
    state_.callDurationSeconds = 0U;
    confirmedAtMs_ = 0U;
}

void MockTelephonyBackend::tick(std::chrono::milliseconds elapsed)
{
    if (elapsed.count() > 0) nowMs_ += static_cast<std::uint64_t>(elapsed.count());
    bool executed = true;
    while (executed) {
        executed = false;
        auto next = std::min_element(
            scheduled_.begin(), scheduled_.end(), [](const auto& left, const auto& right) {
                return left.dueMs < right.dueMs;
            });
        if (next != scheduled_.end() && next->dueMs <= nowMs_) {
            auto action = std::move(next->action);
            scheduled_.erase(next);
            action();
            executed = true;
        }
    }
    if (state_.call == CallState::Active && confirmedAtMs_ != 0U) {
        const auto duration = (nowMs_ - confirmedAtMs_) / 1000U;
        if (duration != state_.callDurationSeconds) {
            state_.callDurationSeconds = duration;
            ++state_.revision;
        }
    }
}

TelephonySnapshot MockTelephonyBackend::getState() const { return state_; }

} // namespace polphone::core
