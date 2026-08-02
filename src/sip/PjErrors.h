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

#include "util/Result.h"

#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace polphone::sip {

[[nodiscard]] std::string statusToString(pj_status_t status);
[[nodiscard]] std::string describe(const pj::Error& error);
[[nodiscard]] util::Error makePjError(const pj::Error& error,
                                      std::string_view operation);

template <typename Function>
[[nodiscard]] auto pjTry(Function&& function, std::string_view operation)
{
    using RawResult = std::invoke_result_t<Function>;
    if constexpr (std::is_void_v<RawResult>) {
        try {
            std::invoke(std::forward<Function>(function));
            return util::Result<void>::success();
        } catch (const pj::Error& error) {
            return util::Result<void>::failure(makePjError(error, operation));
        }
    } else {
        using Value = std::decay_t<RawResult>;
        try {
            return util::Result<Value>::success(
                std::invoke(std::forward<Function>(function)));
        } catch (const pj::Error& error) {
            return util::Result<Value>::failure(makePjError(error, operation));
        }
    }
}

} // namespace polphone::sip

#define POLPHONE_PJ_TRY(expression) \
    ::polphone::sip::pjTry([&]() -> decltype(auto) { return (expression); }, #expression)
