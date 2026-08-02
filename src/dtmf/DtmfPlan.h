/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include "util/Result.h"

#include <string_view>
#include <vector>

namespace polphone::dtmf {

struct DtmfPlanStep {
    enum class Kind {
        Digit,
        Pause
    };

    Kind kind{Kind::Digit};
    char digit{'\0'};
    unsigned onMs{0};
    unsigned offMs{0};
    unsigned pauseMs{0};
};

class DtmfPlan final {
public:
    static constexpr unsigned ExplicitPauseMs = 500U;

    [[nodiscard]] static util::Result<std::vector<DtmfPlanStep>> build(
        std::string_view digits,
        unsigned durationMs,
        unsigned gapMs);
};

} // namespace polphone::dtmf
