/* POLPhone - fachada pública assíncrona. GPL-2.0-only. */

#pragma once

#include "core/CoreApi.h"
#include "core/TelephonyBackend.h"

#include <condition_variable>
#include <deque>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace polphone::core {

class POLPHONE_CORE_API PolPhoneController final {
public:
    using StateChangedHandler = std::function<void(const TelephonySnapshot&)>;

    explicit PolPhoneController(std::unique_ptr<TelephonyBackend> backend);
    ~PolPhoneController();

    PolPhoneController(const PolPhoneController&) = delete;
    PolPhoneController& operator=(const PolPhoneController&) = delete;

    [[nodiscard]] std::future<util::Result<void>> initialize();
    [[nodiscard]] std::future<util::Result<void>> shutdown();
    [[nodiscard]] std::future<util::Result<void>> registerAccount();
    [[nodiscard]] std::future<util::Result<void>> unregisterAccount();
    [[nodiscard]] std::future<util::Result<void>> makeCall(std::string destination);
    [[nodiscard]] std::future<util::Result<void>> answerCall();
    [[nodiscard]] std::future<util::Result<void>> rejectCall();
    [[nodiscard]] std::future<util::Result<void>> hangupCall();
    [[nodiscard]] std::future<util::Result<void>> sendDtmf(
        std::string digits,
        DtmfMethod method,
        unsigned durationMs,
        unsigned gapMs);
    [[nodiscard]] std::future<util::Result<void>> setMuted(bool muted);
    [[nodiscard]] std::future<util::Result<std::vector<AudioDevice>>> listAudioDevices();
    [[nodiscard]] std::future<util::Result<void>> selectCaptureDevice(int id);
    [[nodiscard]] std::future<util::Result<void>> selectPlaybackDevice(int id);
    [[nodiscard]] std::future<util::Result<void>> simulateDemo(DemoScenario scenario);

    [[nodiscard]] TelephonySnapshot getState() const;
    void setStateChangedHandler(StateChangedHandler handler);

private:
    using Operation = std::function<util::Result<void>(TelephonyBackend&)>;
    [[nodiscard]] std::future<util::Result<void>> submit(Operation operation);
    void workerLoop() noexcept;
    void publishIfChanged();

    std::unique_ptr<TelephonyBackend> backend_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::function<void()>> operations_;
    TelephonySnapshot snapshot_;
    StateChangedHandler handler_;
    std::thread worker_;
    bool stopWorker_{false};
};

} // namespace polphone::core
