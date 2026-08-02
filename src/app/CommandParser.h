/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include "dtmf/DtmfMethod.h"
#include "util/Result.h"

#include <optional>
#include <string>
#include <string_view>

namespace polphone::app {

enum class CommandVerb {
    Help,
    Status,
    Devices,
    SetDevice,
    Registration,
    Call,
    Answer,
    Hangup,
    Dtmf,
    DtmfMode,
    DtmfConfig,
    Codecs,
    LogLevel,
    Quit
};

enum class DeviceDirection {
    Capture,
    Playback
};

enum class DtmfConfigField {
    Duration,
    Gap,
    Volume
};

struct Command {
    CommandVerb verb{CommandVerb::Help};
    std::string text;
    std::optional<int> value;
    std::optional<DeviceDirection> deviceDirection;
    std::optional<bool> enabled;
    std::optional<dtmf::DtmfMethod> method;
    std::optional<DtmfConfigField> dtmfField;
    std::optional<int> durationMs;
    std::optional<int> gapMs;
};

class CommandParser final {
public:
    [[nodiscard]] static util::Result<Command> parse(std::string_view line);
};

} // namespace polphone::app
