/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include "util/Result.h"

#include <mutex>
#include <string>

namespace polphone::dtmf {

class DtmfRequestGate final {
public:
    [[nodiscard]] util::Result<void> begin(std::string correlationId);
    void finish() noexcept;
    [[nodiscard]] bool inFlight() const noexcept;
    [[nodiscard]] std::string currentCorrelationId() const;

private:
    mutable std::mutex mutex_;
    bool inFlight_{false};
    std::string currentCorrelationId_;
};

} // namespace polphone::dtmf
