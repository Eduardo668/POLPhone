/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "dtmf/DtmfRequestGate.h"

#include <exception>
#include <utility>

namespace polphone::dtmf {

util::Result<void> DtmfRequestGate::begin(std::string correlationId)
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (inFlight_) {
            return util::Result<void>::failure(
                util::ErrorCode::Runtime,
                "Já existe um envio DTMF em andamento; aguarde sua conclusão.",
                currentCorrelationId_);
        }
        currentCorrelationId_ = std::move(correlationId);
        inFlight_ = true;
        return util::Result<void>::success();
    } catch (const std::exception& error) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Não foi possível iniciar o envio DTMF.",
            error.what());
    } catch (...) {
        return util::Result<void>::failure(
            util::ErrorCode::Runtime,
            "Falha desconhecida ao iniciar o envio DTMF.");
    }
}

void DtmfRequestGate::finish() noexcept
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        inFlight_ = false;
        currentCorrelationId_.clear();
    } catch (...) {
    }
}

bool DtmfRequestGate::inFlight() const noexcept
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        return inFlight_;
    } catch (...) {
        return true;
    }
}

std::string DtmfRequestGate::currentCorrelationId() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return currentCorrelationId_;
}

} // namespace polphone::dtmf
