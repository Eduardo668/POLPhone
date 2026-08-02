/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include "util/Result.h"

namespace polphone::app {

inline constexpr int ExitSuccess = 0;
inline constexpr int ExitConfiguration = 1;
inline constexpr int ExitInitialization = 2;
inline constexpr int ExitRuntime = 3;
inline constexpr int ExitInterrupted = 130;

[[nodiscard]] inline int initializationExitCode(util::ErrorCode code) noexcept
{
    switch (code) {
    case util::ErrorCode::InvalidArgument:
    case util::ErrorCode::NotFound:
    case util::ErrorCode::Io:
    case util::ErrorCode::Parse:
    case util::ErrorCode::Validation:
        return ExitConfiguration;
    case util::ErrorCode::Pjsip:
    case util::ErrorCode::Runtime:
        return ExitInitialization;
    }
    return ExitInitialization;
}

} // namespace polphone::app
