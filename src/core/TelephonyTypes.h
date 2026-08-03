/*
 * POLPhone - núcleo compartilhado entre CLI e GUI.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include "core/CoreApi.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace polphone::core {

enum class ApplicationState {
    Stopped,
    Initializing,
    Ready,
    ShuttingDown,
    Failed
};

enum class RegistrationState {
    Disconnected,
    Connecting,
    Connected,
    Disconnecting,
    Failed
};

enum class CallState {
    Idle,
    Incoming,
    Calling,
    Connecting,
    Confirmed,
    Ending,
    Failed
};

enum class MediaState {
    Inactive,
    Active,
    Failed
};

enum class DtmfMethod {
    Automatic,
    Rfc4733,
    Inband,
    SipInfo
};

enum class AudioDirection {
    Capture,
    Playback
};

enum class DemoScenario {
    IncomingCall,
    RegistrationFailure,
    CallFailure,
    ConnectionLoss
};

struct AudioDevice {
    int id{-1};
    std::string name;
    bool capture{false};
    bool playback{false};
};

struct TelephonySnapshot {
    std::uint64_t revision{0};
    ApplicationState application{ApplicationState::Stopped};
    RegistrationState registration{RegistrationState::Disconnected};
    CallState call{CallState::Idle};
    MediaState media{MediaState::Inactive};
    std::string maskedRemote;
    std::uint64_t callDurationSeconds{0};
    std::string codec;
    bool muted{false};
    bool dtmfInFlight{false};
    DtmfMethod dtmfMethod{DtmfMethod::Automatic};
    unsigned dtmfDurationMs{160};
    unsigned dtmfGapMs{100};
    int inbandVolumeDbm0{-10};
    std::vector<AudioDevice> devices;
    std::optional<int> captureDevice;
    std::optional<int> playbackDevice;
    std::string endpointState;
    std::string friendlyError;
    std::string technicalDetail;
    std::vector<std::string> sanitizedLogs;
    bool demoMode{false};
};

struct CommandAvailability {
    bool registerAccount{false};
    bool unregisterAccount{false};
    bool makeCall{false};
    bool answerCall{false};
    bool rejectCall{false};
    bool hangupCall{false};
    bool mute{false};
    bool keypad{false};
    bool sendDtmf{false};
};

[[nodiscard]] POLPHONE_CORE_API std::string_view applicationStateText(ApplicationState state) noexcept;
[[nodiscard]] POLPHONE_CORE_API std::string_view registrationStateText(RegistrationState state) noexcept;
[[nodiscard]] POLPHONE_CORE_API std::string_view callStateText(CallState state) noexcept;
[[nodiscard]] POLPHONE_CORE_API std::string_view mediaStateText(MediaState state) noexcept;
[[nodiscard]] POLPHONE_CORE_API std::string_view dtmfMethodText(DtmfMethod method) noexcept;

} // namespace polphone::core
