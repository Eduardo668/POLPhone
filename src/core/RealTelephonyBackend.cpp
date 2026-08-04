/* POLPhone - adaptador do motor SIP real para a fachada pública. GPL-2.0-only. */

#include "core/RealTelephonyBackend.h"

#include "logging/Redactor.h"
#include "core/Presentation.h"

#include <utility>

namespace polphone::core {
namespace {

RegistrationState mapRegistration(app::RegistrationState value) noexcept
{
    switch (value) {
    case app::RegistrationState::Registering: return RegistrationState::Connecting;
    case app::RegistrationState::Registered: return RegistrationState::Connected;
    case app::RegistrationState::Unregistering: return RegistrationState::Disconnecting;
    case app::RegistrationState::Failed: return RegistrationState::Failed;
    case app::RegistrationState::Unconfigured:
    case app::RegistrationState::Unregistered: return RegistrationState::Disconnected;
    }
    return RegistrationState::Disconnected;
}

CallDirection mapDirection(app::CallDirection value) noexcept
{
    switch (value) {
    case app::CallDirection::None: return CallDirection::None;
    case app::CallDirection::Incoming: return CallDirection::Incoming;
    case app::CallDirection::Outgoing: return CallDirection::Outgoing;
    }
    return CallDirection::None;
}

CallState mapCall(app::CallState value, app::CallDirection direction) noexcept
{
    switch (value) {
    case app::CallState::Incoming: return CallState::IncomingRinging;
    case app::CallState::Calling: return CallState::OutgoingDialing;
    case app::CallState::Early:
        return direction == app::CallDirection::Incoming
            ? CallState::IncomingRinging : CallState::OutgoingRinging;
    case app::CallState::Connecting: return CallState::Connecting;
    case app::CallState::Confirmed: return CallState::Active;
    case app::CallState::Disconnected: return CallState::Disconnected;
    case app::CallState::Idle: return CallState::Idle;
    }
    return CallState::Idle;
}

dtmf::DtmfMethod toEngineMethod(DtmfMethod selected, std::string_view configured)
{
    if (selected == DtmfMethod::Automatic) {
        return dtmf::parseMethod(configured).value_or(dtmf::DtmfMethod::Rfc4733);
    }
    if (selected == DtmfMethod::Inband) return dtmf::DtmfMethod::Inband;
    if (selected == DtmfMethod::SipInfo) return dtmf::DtmfMethod::SipInfo;
    return dtmf::DtmfMethod::Rfc4733;
}

DtmfMethod fromEngineMethod(std::string_view method) noexcept
{
    if (method == "inband") return DtmfMethod::Inband;
    if (method == "info") return DtmfMethod::SipInfo;
    return DtmfMethod::Rfc4733;
}

DtmfMethod fromEngineMethod(dtmf::DtmfMethod method) noexcept
{
    switch (method) {
    case dtmf::DtmfMethod::Rfc4733: return DtmfMethod::Rfc4733;
    case dtmf::DtmfMethod::Inband: return DtmfMethod::Inband;
    case dtmf::DtmfMethod::SipInfo: return DtmfMethod::SipInfo;
    }
    return DtmfMethod::Rfc4733;
}

} // namespace

RealTelephonyBackend::RealTelephonyBackend(std::filesystem::path configPath)
{
    options_.configPath = std::move(configPath);
    options_.enableConsoleLogging = false;
    state_.endpointState = "Parado";
}

util::Result<void> RealTelephonyBackend::initialize()
{
    if (application_) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "O motor SIP já foi inicializado.");
    }
    state_.application = ApplicationState::Initializing;
    state_.endpointState = "Inicializando";
    ++state_.revision;
    application_ = std::make_unique<app::Application>(options_);
    auto initialized = application_->initialize();
    if (!initialized) {
        recordError(initialized.error());
        application_.reset();
        return initialized;
    }
    state_.application = ApplicationState::Ready;
    state_.endpointState = "STARTED";
    const auto initial = application_->status();
    configuredDtmf_ = DtmfRuntimeSettings{
        fromEngineMethod(initial.dtmfMethod),
        static_cast<unsigned>(initial.dtmf.durationMs),
        static_cast<unsigned>(initial.dtmf.gapMs),
        initial.dtmf.volumeDbm0};
    const auto devices = listAudioDevices();
    if (!devices) {
        state_.technicalDetail = logging::Redactor::redact(devices.error().detail);
    }
    refresh();
    return util::Result<void>::success();
}

util::Result<void> RealTelephonyBackend::shutdown()
{
    if (!application_) return util::Result<void>::success();
    state_.application = ApplicationState::ShuttingDown;
    ++state_.revision;
    application_->shutdown();
    application_.reset();
    state_ = TelephonySnapshot{};
    state_.application = ApplicationState::Stopped;
    state_.endpointState = "Parado";
    ++state_.revision;
    return util::Result<void>::success();
}

util::Result<void> RealTelephonyBackend::registerAccount()
{
    return application_ ? application_->setRegistrationEnabled(true)
        : util::Result<void>::failure(util::ErrorCode::Runtime, "Motor SIP não inicializado.");
}

util::Result<void> RealTelephonyBackend::unregisterAccount()
{
    return application_ ? application_->setRegistrationEnabled(false)
        : util::Result<void>::failure(util::ErrorCode::Runtime, "Motor SIP não inicializado.");
}

util::Result<void> RealTelephonyBackend::makeCall(std::string_view destination)
{
    if (!application_) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "Motor SIP não inicializado.");
    }
    auto result = application_->makeCall(destination);
    if (result) refresh();
    return result;
}

util::Result<void> RealTelephonyBackend::answerCall()
{
    if (!application_) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "Motor SIP não inicializado.");
    }
    auto result = application_->answerCall();
    if (result) {
        state_.call = CallState::Connecting;
        ++state_.revision;
    }
    return result;
}

util::Result<void> RealTelephonyBackend::rejectCall()
{
    if (!application_) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "Motor SIP não inicializado.");
    }
    auto result = application_->rejectCall();
    if (result) {
        state_.call = CallState::Disconnecting;
        ++state_.revision;
    }
    return result;
}

util::Result<void> RealTelephonyBackend::hangupCall()
{
    if (!application_) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "Motor SIP não inicializado.");
    }
    auto result = application_->hangupCall();
    if (result) {
        state_.call = CallState::Disconnecting;
        ++state_.revision;
    }
    return result;
}

util::Result<void> RealTelephonyBackend::applyDtmfSettings(
    const DtmfRuntimeSettings& settings)
{
    if (!application_) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime, "Motor SIP não inicializado.");
    }
    if (settings.method == DtmfMethod::Automatic) {
        return util::Result<void>::failure(
            util::ErrorCode::InvalidArgument,
            "Escolha RFC 4733, SIP INFO ou In-band como método DTMF padrão.");
    }
    configuredDtmf_ = settings;
    const auto method = toEngineMethod(settings.method, {});
    const auto applied = application_->applyDtmfSettings(
        method,
        static_cast<int>(settings.durationMs),
        static_cast<int>(settings.gapMs),
        settings.inbandVolumeDbm0);
    if (!applied) {
        state_.friendlyError = applied.error().message;
        state_.technicalDetail = logging::Redactor::redact(applied.error().detail);
        ++state_.revision;
        return applied;
    }
    state_.friendlyError.clear();
    state_.technicalDetail.clear();
    refresh();
    return util::Result<void>::success();
}

util::Result<void> RealTelephonyBackend::sendDtmf(
    std::string_view digits,
    DtmfMethod method)
{
    if (!application_) {
        return util::Result<void>::failure(util::ErrorCode::Runtime, "Motor SIP não inicializado.");
    }
    const auto status = application_->status();
    auto sent = application_->sendDtmf(
        digits,
        toEngineMethod(method, status.dtmfMethod),
        std::nullopt,
        std::nullopt);
    if (!sent) return util::Result<void>::failure(sent.error());
    refresh();
    return util::Result<void>::success();
}

util::Result<void> RealTelephonyBackend::setMuted(bool muted)
{
    return application_ ? application_->setMuted(muted)
        : util::Result<void>::failure(util::ErrorCode::Runtime, "Motor SIP não inicializado.");
}

util::Result<std::vector<AudioDevice>> RealTelephonyBackend::listAudioDevices()
{
    if (!application_) {
        return util::Result<std::vector<AudioDevice>>::failure(
            util::ErrorCode::Runtime, "Motor SIP não inicializado.");
    }
    auto listed = application_->listAudioDevices();
    if (!listed) return util::Result<std::vector<AudioDevice>>::failure(listed.error());
    std::vector<AudioDevice> devices;
    devices.reserve(listed.value().size());
    for (const auto& device : listed.value()) {
        devices.push_back(AudioDevice{
            device.id, device.name, device.inputCount > 0U, device.outputCount > 0U});
    }
    state_.devices = devices;
    ++state_.revision;
    return util::Result<std::vector<AudioDevice>>::success(std::move(devices));
}

util::Result<void> RealTelephonyBackend::selectAudioDevice(
    AudioDirection direction,
    int id)
{
    if (!application_) {
        return util::Result<void>::failure(util::ErrorCode::Runtime, "Motor SIP não inicializado.");
    }
    return application_->selectAudioDevice(
        direction == AudioDirection::Capture
            ? audio::AudioDeviceDirection::Capture : audio::AudioDeviceDirection::Playback,
        id);
}

util::Result<void> RealTelephonyBackend::simulate(DemoScenario)
{
    return util::Result<void>::failure(
        util::ErrorCode::InvalidArgument,
        "Cenários simulados estão disponíveis somente com --demo.");
}

void RealTelephonyBackend::recordError(const util::Error& error)
{
    state_.application = ApplicationState::Failed;
    state_.friendlyError = error.message;
    state_.technicalDetail = logging::Redactor::redact(error.detail);
    ++state_.revision;
}

void RealTelephonyBackend::refresh()
{
    if (!application_) return;
    const auto current = application_->status();
    const auto previousCall = state_.call;
    state_.registration = mapRegistration(current.registration.state);
    state_.callDirection = mapDirection(current.call.direction);
    state_.call = mapCall(current.call.state, current.call.direction);
    // Caller ID é dado funcional da chamada. Extraímos antes de preparar a
    // variante mascarada de diagnóstico para não transformar o ramal em ****.
    const auto caller = parseCallerIdentity(current.call.remoteUri);
    state_.maskedRemote = logging::Redactor::redact(current.call.remoteUri);
    state_.remoteDisplayName = caller.displayName;
    state_.remoteNumber = caller.number;
    state_.media = current.mediaActive ? MediaState::Active : MediaState::Inactive;
    state_.latestSipCode = current.latestSipCode;
    state_.registrarUri = logging::Redactor::redact(current.registrarUri);
    state_.transport = current.transport;
    state_.sipUsername = logging::Redactor::redact(current.sipUsername);
    state_.userAgent = current.userAgent;
    state_.localRtp = logging::Redactor::redact(current.callDiagnostics.localRtp);
    state_.remoteRtp = logging::Redactor::redact(current.callDiagnostics.remoteRtp);
    state_.packetsSent = current.callDiagnostics.packetsSent;
    state_.packetsReceived = current.callDiagnostics.packetsReceived;
    state_.packetsLost = current.callDiagnostics.packetsLost;
    state_.jitterMs = current.callDiagnostics.jitterMs;
    state_.hasRtpStatistics = current.callDiagnostics.hasRtpStatistics;
    state_.ringtoneEnabled = current.behavior.ringtoneEnabled;
    state_.topmostOnIncomingCall = current.behavior.topmostOnIncomingCall;
    if (!current.callDiagnostics.codec.empty()) {
        state_.codec = current.callDiagnostics.codec;
    }
    state_.muted = current.muted;
    state_.captureDevice = current.captureDevice;
    state_.playbackDevice = current.playbackDevice;
    state_.dtmfConfiguredMethod = configuredDtmf_.method;
    state_.dtmfEffectiveMethod = fromEngineMethod(current.dtmfMethod);
    state_.lastDtmfMethod = current.lastDtmfMethod.has_value()
        ? std::optional<DtmfMethod>{fromEngineMethod(*current.lastDtmfMethod)}
        : std::nullopt;
    state_.lastDtmfResult = current.lastDtmfResult;
    state_.dtmfDurationMs = static_cast<unsigned>(current.dtmf.durationMs);
    state_.dtmfGapMs = static_cast<unsigned>(current.dtmf.gapMs);
    state_.inbandVolumeDbm0 = current.dtmf.volumeDbm0;
    if (state_.call != CallState::Active) {
        confirmedDuration_ = std::chrono::milliseconds::zero();
        state_.callDurationSeconds = 0U;
    } else if (previousCall != CallState::Active) {
        confirmedDuration_ = std::chrono::milliseconds::zero();
    }
    for (const auto& event : application_->drainEvents()) {
        if (event.category == "media") {
            constexpr std::string_view connectedPrefix =
                "Áudio bidirecional conectado: codec=";
            constexpr std::string_view activePrefix = "Áudio ativo: ";
            if (event.text.compare(0, connectedPrefix.size(), connectedPrefix) == 0) {
                const auto value = event.text.substr(connectedPrefix.size());
                state_.codec = value.substr(0, value.find(' '));
            } else if (event.text.compare(0, activePrefix.size(), activePrefix) == 0) {
                const auto value = event.text.substr(activePrefix.size());
                state_.codec = value.substr(0, value.find('.'));
            }
        }
        state_.sanitizedLogs.push_back(logging::Redactor::redact(
            "[" + event.category + "] " + event.text));
    }
    if (state_.sanitizedLogs.size() > 80U) {
        state_.sanitizedLogs.erase(
            state_.sanitizedLogs.begin(), state_.sanitizedLogs.end() - 80);
    }
    if (state_.call != CallState::Active) state_.codec.clear();
    ++state_.revision;
}

void RealTelephonyBackend::tick(std::chrono::milliseconds elapsed)
{
    if (!application_) return;
    static_cast<void>(application_->reapCalls());
    if (state_.call == CallState::Active) {
        confirmedDuration_ += elapsed;
    }
    refresh();
    state_.callDurationSeconds = static_cast<std::uint64_t>(confirmedDuration_.count() / 1000);
}

TelephonySnapshot RealTelephonyBackend::getState() const { return state_; }

} // namespace polphone::core
