/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "dtmf/DtmfPlan.h"

#include <exception>
#include <string>
#include <utility>

namespace polphone::dtmf {
namespace {

bool isDtmfDigit(char digit) noexcept
{
    return (digit >= '0' && digit <= '9') || digit == '*' || digit == '#'
        || (digit >= 'A' && digit <= 'D');
}

} // namespace

util::Result<std::vector<DtmfPlanStep>> DtmfPlan::build(
    std::string_view digits,
    unsigned durationMs,
    unsigned gapMs)
{
    if (digits.empty()) {
        return util::Result<std::vector<DtmfPlanStep>>::failure(
            util::ErrorCode::InvalidArgument,
            "Informe ao menos um dígito DTMF.");
    }
    if (durationMs < 40U || durationMs > 2000U) {
        return util::Result<std::vector<DtmfPlanStep>>::failure(
            util::ErrorCode::InvalidArgument,
            "A duração DTMF deve estar entre 40 e 2000 ms.",
            "duration=" + std::to_string(durationMs));
    }
    if (gapMs < 20U || gapMs > 2000U) {
        return util::Result<std::vector<DtmfPlanStep>>::failure(
            util::ErrorCode::InvalidArgument,
            "O intervalo DTMF deve estar entre 20 e 2000 ms.",
            "gap=" + std::to_string(gapMs));
    }

    try {
        std::vector<DtmfPlanStep> steps;
        steps.reserve(digits.size());
        bool hasDigit = false;
        for (const char digit : digits) {
            if (digit == ',') {
                steps.push_back(DtmfPlanStep{
                    DtmfPlanStep::Kind::Pause, '\0', 0U, 0U, ExplicitPauseMs});
                continue;
            }
            if (!isDtmfDigit(digit)) {
                return util::Result<std::vector<DtmfPlanStep>>::failure(
                    util::ErrorCode::InvalidArgument,
                    "Sequência DTMF inválida; use somente 0-9, *, #, A-D e vírgula.",
                    std::string(1U, digit));
            }
            hasDigit = true;
            steps.push_back(DtmfPlanStep{
                DtmfPlanStep::Kind::Digit, digit, durationMs, gapMs, 0U});
        }
        if (!hasDigit) {
            return util::Result<std::vector<DtmfPlanStep>>::failure(
                util::ErrorCode::InvalidArgument,
                "A sequência contém apenas pausas; informe ao menos um dígito DTMF.");
        }
        return util::Result<std::vector<DtmfPlanStep>>::success(std::move(steps));
    } catch (const std::exception& error) {
        return util::Result<std::vector<DtmfPlanStep>>::failure(
            util::ErrorCode::Runtime,
            "Não foi possível montar o plano DTMF; libere memória e tente novamente.",
            error.what());
    } catch (...) {
        return util::Result<std::vector<DtmfPlanStep>>::failure(
            util::ErrorCode::Runtime,
            "Falha desconhecida ao montar o plano DTMF.");
    }
}

} // namespace polphone::dtmf
