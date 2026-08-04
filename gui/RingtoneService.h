/* POLPhone - ringtone nativo e idempotente para chamadas recebidas. GPL-2.0-only. */

#pragma once

#include <atomic>

namespace polphone::gui {

class RingtoneService final {
public:
    ~RingtoneService();
    void start() noexcept;
    void stop() noexcept;
    [[nodiscard]] bool playing() const noexcept;

private:
    std::atomic<bool> playing_{false};
};

} // namespace polphone::gui
