/* POLPhone - núcleo compartilhado entre CLI e GUI. GPL-2.0-only. */

#include "core/TelephonyTypes.h"

namespace polphone::core {

std::string_view applicationStateText(ApplicationState state) noexcept
{
    switch (state) {
    case ApplicationState::Stopped: return "Parado";
    case ApplicationState::Initializing: return "Inicializando";
    case ApplicationState::Ready: return "Pronto";
    case ApplicationState::ShuttingDown: return "Encerrando";
    case ApplicationState::Failed: return "Falha";
    }
    return "Desconhecido";
}

std::string_view registrationStateText(RegistrationState state) noexcept
{
    switch (state) {
    case RegistrationState::Disconnected: return "Desconectado";
    case RegistrationState::Connecting: return "Conectando";
    case RegistrationState::Connected: return "Conectado";
    case RegistrationState::Disconnecting: return "Desconectando";
    case RegistrationState::Failed: return "Falha no registro";
    }
    return "Desconhecido";
}

std::string_view callStateText(CallState state) noexcept
{
    switch (state) {
    case CallState::Idle: return "Sem chamada";
    case CallState::OutgoingDialing: return "Chamando…";
    case CallState::OutgoingRinging: return "Destino tocando";
    case CallState::IncomingRinging: return "Chamada recebida";
    case CallState::Connecting: return "Conectando chamada";
    case CallState::Active: return "Em chamada";
    case CallState::Disconnecting: return "Encerrando";
    case CallState::Disconnected: return "Chamada encerrada";
    case CallState::Error: return "Falha na chamada";
    }
    return "Desconhecido";
}

std::string_view callDirectionText(CallDirection direction) noexcept
{
    switch (direction) {
    case CallDirection::None: return "Nenhuma";
    case CallDirection::Incoming: return "Recebida";
    case CallDirection::Outgoing: return "Originada";
    }
    return "Desconhecida";
}

std::string_view mediaStateText(MediaState state) noexcept
{
    switch (state) {
    case MediaState::Inactive: return "Áudio inativo";
    case MediaState::Active: return "Áudio ativo";
    case MediaState::Failed: return "Falha de áudio";
    }
    return "Desconhecido";
}

std::string_view dtmfMethodText(DtmfMethod method) noexcept
{
    switch (method) {
    case DtmfMethod::Automatic: return "Automático";
    case DtmfMethod::Rfc4733: return "RFC 4733";
    case DtmfMethod::Inband: return "In-band";
    case DtmfMethod::SipInfo: return "SIP INFO";
    }
    return "Desconhecido";
}

} // namespace polphone::core
