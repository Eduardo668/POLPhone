/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

// O PJSIP deve vir antes dos headers do Windows para evitar conflito de Winsock.
#include <pjsua2.hpp>

#include "logging/Logger.h"

namespace polphone::logging {

class PjLogWriter final : public pj::LogWriter {
public:
    explicit PjLogWriter(Logger& logger) noexcept;

    // Callback chamado por threads internas do PJSIP. Nenhuma exceção pode
    // atravessar esta fronteira.
    void write(const pj::LogEntry& entry) noexcept override;

    [[nodiscard]] static LogLevel mapLevel(int pjsipLevel) noexcept;

private:
    Logger& logger_;
};

} // namespace polphone::logging
