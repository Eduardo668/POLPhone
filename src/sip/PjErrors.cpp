/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "sip/PjErrors.h"

#include <array>
#include <string>
#include <utility>

namespace polphone::sip {
namespace {

std::string baseName(std::string_view path)
{
    const std::size_t separator = path.find_last_of("/\\");
    return std::string(separator == std::string_view::npos
                           ? path
                           : path.substr(separator + 1U));
}

} // namespace

std::string statusToString(pj_status_t status)
{
    std::array<char, PJ_ERR_MSG_SIZE> buffer{};
    const pj_str_t description = pj_strerror(status, buffer.data(), buffer.size());
    if (description.ptr == nullptr || description.slen <= 0) {
        return "erro PJSIP desconhecido";
    }
    return std::string(description.ptr, static_cast<std::size_t>(description.slen));
}

std::string describe(const pj::Error& error)
{
    std::string detail = "status=" + std::to_string(error.status)
        + " (" + statusToString(error.status) + ")";
    if (!error.title.empty()) detail += " title=" + error.title;
    if (!error.reason.empty()) detail += " reason=" + error.reason;
    if (!error.srcFile.empty()) {
        detail += " source=" + baseName(error.srcFile) + ":" + std::to_string(error.srcLine);
    }
    return detail;
}

util::Error makePjError(const pj::Error& error, std::string_view operation)
{
    std::string detail;
    if (!operation.empty()) detail = "operation=" + std::string(operation) + " ";
    detail += describe(error);
    return util::Error{
        util::ErrorCode::Pjsip,
        "Uma operação PJSIP falhou; revise a configuração e o detalhe técnico.",
        std::move(detail)};
}

} // namespace polphone::sip
