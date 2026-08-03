/* POLPhone - fachada pública assíncrona. GPL-2.0-only. */

#include "core/PolPhoneController.h"

#include <chrono>
#include <exception>
#include <utility>

namespace polphone::core {
namespace {

util::Result<void> unexpectedFailure(const char* detail)
{
    return util::Result<void>::failure(
        util::ErrorCode::Runtime,
        "Uma operação interna do telefone falhou de forma inesperada.",
        detail);
}

} // namespace

PolPhoneController::PolPhoneController(std::unique_ptr<TelephonyBackend> backend)
    : backend_(std::move(backend))
{
    if (backend_) snapshot_ = backend_->getState();
    worker_ = std::thread(&PolPhoneController::workerLoop, this);
}

PolPhoneController::~PolPhoneController()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handler_ = {};
        stopWorker_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

std::future<util::Result<void>> PolPhoneController::submit(Operation operation)
{
    auto promise = std::make_shared<std::promise<util::Result<void>>>();
    auto future = promise->get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopWorker_) {
            promise->set_value(util::Result<void>::failure(
                util::ErrorCode::Runtime, "O controlador já está sendo encerrado."));
            return future;
        }
        operations_.push_back([this, promise, operation = std::move(operation)]() mutable {
            try {
                if (!backend_) {
                    promise->set_value(unexpectedFailure("backend ausente"));
                    return;
                }
                promise->set_value(operation(*backend_));
            } catch (const std::exception& error) {
                promise->set_value(unexpectedFailure(error.what()));
            } catch (...) {
                promise->set_value(unexpectedFailure("exceção desconhecida"));
            }
        });
    }
    condition_.notify_one();
    return future;
}

std::future<util::Result<void>> PolPhoneController::initialize()
{
    return submit([](TelephonyBackend& backend) { return backend.initialize(); });
}

std::future<util::Result<void>> PolPhoneController::shutdown()
{
    return submit([](TelephonyBackend& backend) { return backend.shutdown(); });
}

std::future<util::Result<void>> PolPhoneController::registerAccount()
{
    return submit([](TelephonyBackend& backend) { return backend.registerAccount(); });
}

std::future<util::Result<void>> PolPhoneController::unregisterAccount()
{
    return submit([](TelephonyBackend& backend) { return backend.unregisterAccount(); });
}

std::future<util::Result<void>> PolPhoneController::makeCall(std::string destination)
{
    return submit([destination = std::move(destination)](TelephonyBackend& backend) {
        return backend.makeCall(destination);
    });
}

std::future<util::Result<void>> PolPhoneController::answerCall()
{
    return submit([](TelephonyBackend& backend) { return backend.answerCall(); });
}

std::future<util::Result<void>> PolPhoneController::rejectCall()
{
    return submit([](TelephonyBackend& backend) { return backend.rejectCall(); });
}

std::future<util::Result<void>> PolPhoneController::hangupCall()
{
    return submit([](TelephonyBackend& backend) { return backend.hangupCall(); });
}

std::future<util::Result<void>> PolPhoneController::sendDtmf(
    std::string digits,
    DtmfMethod method,
    unsigned durationMs,
    unsigned gapMs)
{
    return submit([
        digits = std::move(digits), method, durationMs, gapMs](TelephonyBackend& backend) {
        return backend.sendDtmf(digits, method, durationMs, gapMs);
    });
}

std::future<util::Result<void>> PolPhoneController::setMuted(bool muted)
{
    return submit([muted](TelephonyBackend& backend) { return backend.setMuted(muted); });
}

std::future<util::Result<std::vector<AudioDevice>>> PolPhoneController::listAudioDevices()
{
    auto promise = std::make_shared<
        std::promise<util::Result<std::vector<AudioDevice>>>>();
    auto future = promise->get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopWorker_) {
            promise->set_value(util::Result<std::vector<AudioDevice>>::failure(
                util::ErrorCode::Runtime, "O controlador já está sendo encerrado."));
            return future;
        }
        operations_.push_back([this, promise] {
            try {
                if (!backend_) {
                    promise->set_value(util::Result<std::vector<AudioDevice>>::failure(
                        util::ErrorCode::Runtime,
                        "Uma operação interna do telefone falhou de forma inesperada.",
                        "backend ausente"));
                    return;
                }
                promise->set_value(backend_->listAudioDevices());
            } catch (const std::exception& error) {
                promise->set_value(util::Result<std::vector<AudioDevice>>::failure(
                    util::ErrorCode::Runtime,
                    "Não foi possível listar os dispositivos de áudio.",
                    error.what()));
            } catch (...) {
                promise->set_value(util::Result<std::vector<AudioDevice>>::failure(
                    util::ErrorCode::Runtime,
                    "Não foi possível listar os dispositivos de áudio."));
            }
        });
    }
    condition_.notify_one();
    return future;
}

std::future<util::Result<void>> PolPhoneController::selectCaptureDevice(int id)
{
    return submit([id](TelephonyBackend& backend) {
        return backend.selectAudioDevice(AudioDirection::Capture, id);
    });
}

std::future<util::Result<void>> PolPhoneController::selectPlaybackDevice(int id)
{
    return submit([id](TelephonyBackend& backend) {
        return backend.selectAudioDevice(AudioDirection::Playback, id);
    });
}

std::future<util::Result<void>> PolPhoneController::simulateDemo(DemoScenario scenario)
{
    return submit([scenario](TelephonyBackend& backend) { return backend.simulate(scenario); });
}

TelephonySnapshot PolPhoneController::getState() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void PolPhoneController::setStateChangedHandler(StateChangedHandler handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    handler_ = std::move(handler);
}

void PolPhoneController::publishIfChanged()
{
    TelephonySnapshot next = backend_->getState();
    StateChangedHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (next.revision == snapshot_.revision) return;
        snapshot_ = next;
        handler = handler_;
    }
    if (handler) {
        try {
            handler(next);
        } catch (...) {
            // Consumidores não podem encerrar a thread do controlador.
        }
    }
}

void PolPhoneController::workerLoop() noexcept
{
    using namespace std::chrono_literals;
    try {
        auto previous = std::chrono::steady_clock::now();
        for (;;) {
            std::function<void()> operation;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait_for(lock, 50ms, [this] {
                    return stopWorker_ || !operations_.empty();
                });
                if (stopWorker_ && operations_.empty()) break;
                if (!operations_.empty()) {
                    operation = std::move(operations_.front());
                    operations_.pop_front();
                }
            }
            if (operation) operation();
            const auto now = std::chrono::steady_clock::now();
            if (backend_) {
                backend_->tick(
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - previous));
                publishIfChanged();
            }
            previous = now;
        }
        if (backend_) {
            static_cast<void>(backend_->shutdown());
            backend_->tick(250ms);
            publishIfChanged();
        }
    } catch (...) {
        // Destrutores não propagam exceções. As operações individuais já são traduzidas.
    }
}

} // namespace polphone::core
