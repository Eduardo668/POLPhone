/* POLPhone - adaptador do motor SIP real para a fachada pública. GPL-2.0-only. */

#include "core/RealTelephonyBackend.h"

#include "logging/Redactor.h"

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

CallState mapCall(app::CallState value) noexcept
{
    switch (value) {
    case app::CallState::Incoming: return CallState::Incoming;
    case app::CallState::Calling:
    case app::CallState::Early: return CallState::Calling;
    case app::CallState::Connecting: return CallState::Connecting;
    case app::CallState::Confirmed: return CallState::Confirmed;
    case app::CallState::Idle:
    case app::CallState::Disconnected: return CallState::Idle;
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
    return application_ ? application_->makeCall(destination)
        : util::Result<void>::failure(util::ErrorCode::Runtime, "Motor SIP não inicializado.");
}

util::Result<void> RealTelephonyBackend::answerCall()
{
    return application_ ? application_->answerCall()
        : util::Result<void>::failure(util::ErrorCode::Runtime, "Motor SIP não inicializado.");
}

util::Result<void> RealTelephonyBackend::rejectCall()
{
    return application_ ? application_->rejectCall()
        : util::Result<void>::failure(util::ErrorCode::Runtime, "Motor SIP não inicializado.");
}

util::Result<void> RealTelephonyBackend::hangupCall()
{
    return application_ ? application_->hangupCall()
        : util::Result<void>::failure(util::ErrorCode::Runtime, "Motor SIP não inicializado.");
}

util::Result<void> RealTelephonyBackend::sendDtmf(
    std::string_view digits,
    DtmfMethod method,
    unsigned durationMs,
    unsigned gapMs)
{
    if (!application_) {
        return util::Result<void>::failure(util::ErrorCode::Runtime, "Motor SIP não inicializado.");
    }
    const auto status = application_->status();
    auto sent = application_->sendDtmf(
        digits,
        toEngineMethod(method, status.dtmfMethod),
        static_cast<int>(durationMs),
        static_cast<int>(gapMs));
    if (!sent) return util::Result<void>::failure(sent.error());
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
    state_.call = mapCall(current.call.state);
    state_.maskedRemote = logging::Redactor::redact(current.call.remoteUri);
    state_.media = current.mediaActive ? MediaState::Active : MediaState::Inactive;
    state_.muted = current.muted;
    state_.captureDevice = current.captureDevice;
    state_.playbackDevice = current.playbackDevice;
    state_.dtmfMethod = fromEngineMethod(current.dtmfMethod);
    state_.dtmfDurationMs = static_cast<unsigned>(current.dtmf.durationMs);
    state_.dtmfGapMs = static_cast<unsigned>(current.dtmf.gapMs);
    state_.inbandVolumeDbm0 = current.dtmf.volumeDbm0;
    if (state_.call != CallState::Confirmed) {
        confirmedDuration_ = std::chrono::milliseconds::zero();
        state_.callDurationSeconds = 0U;
    } else if (previousCall != CallState::Confirmed) {
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
    if (state_.call != CallState::Confirmed) state_.codec.clear();
    ++state_.revision;
}

void RealTelephonyBackend::tick(std::chrono::milliseconds elapsed)
{
    if (!application_) return;
    static_cast<void>(application_->reapCalls());
    if (state_.call == CallState::Confirmed) {
        confirmedDuration_ += elapsed;
    }
    refresh();
    state_.callDurationSeconds = static_cast<std::uint64_t>(confirmedDuration_.count() / 1000);
}

TelephonySnapshot RealTelephonyBackend::getState() const { return state_; }

} // namespace polphone::core
