/* POLPhone - ringtone nativo e idempotente para chamadas recebidas. GPL-2.0-only. */

#include "RingtoneService.h"

#include <Windows.h>
#include <mmsystem.h>

namespace polphone::gui {

RingtoneService::~RingtoneService() { stop(); }

void RingtoneService::start() noexcept
{
    if (playing_.exchange(true)) return;
    // Usa o som de chamada configurado no próprio Windows; nenhum áudio de
    // terceiros é incorporado ao binário ou ao repositório.
    if (!PlaySoundW(L"SystemExclamation", nullptr,
                    SND_ALIAS | SND_ASYNC | SND_LOOP | SND_NODEFAULT)) {
        playing_ = false;
    }
}

void RingtoneService::stop() noexcept
{
    if (!playing_.exchange(false)) return;
    PlaySoundW(nullptr, nullptr, 0);
}

bool RingtoneService::playing() const noexcept { return playing_.load(); }

} // namespace polphone::gui
