/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "logging/Logger.h"

#include "logging/Redactor.h"
#include "util/Time.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <system_error>
#include <utility>

namespace polphone::logging {
namespace {

bool accepts(LogLevel level, LogLevel maxLevel) noexcept
{
    return level != LogLevel::Off && maxLevel != LogLevel::Off
        && logLevelNumber(level) <= logLevelNumber(maxLevel);
}

util::Result<void> ioFailure(std::string message, std::string detail = {})
{
    return util::Result<void>::failure(
        util::ErrorCode::Io, std::move(message), std::move(detail));
}

class ReentryGuard final {
public:
    explicit ReentryGuard(const Logger* logger) noexcept
        : previous_(activeLogger_)
    {
        if (activeLogger_ == logger) {
            entered_ = false;
            return;
        }
        activeLogger_ = logger;
        entered_ = true;
    }

    ~ReentryGuard()
    {
        if (entered_) {
            activeLogger_ = previous_;
        }
    }

    [[nodiscard]] bool entered() const noexcept { return entered_; }

private:
    static thread_local const Logger* activeLogger_;
    const Logger* previous_{nullptr};
    bool entered_{false};
};

thread_local const Logger* ReentryGuard::activeLogger_ = nullptr;

} // namespace

LogLevel logLevelFromNumber(int level) noexcept
{
    if (level <= 0) {
        return LogLevel::Off;
    }
    if (level >= static_cast<int>(LogLevel::Trace)) {
        return LogLevel::Trace;
    }
    return static_cast<LogLevel>(level);
}

unsigned logLevelNumber(LogLevel level) noexcept
{
    return static_cast<unsigned>(level);
}

std::string_view logLevelName(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::Off: return "OFF  ";
    case LogLevel::Fatal: return "FATAL";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Warning: return "WARN ";
    case LogLevel::Info: return "INFO ";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Trace: return "TRACE";
    }
    return "UNKWN";
}

std::string formatLogEntry(const LogEntry& entry)
{
    std::string formatted;
    formatted.reserve(entry.timestamp.size() + entry.component.size() + entry.message.size()
                      + entry.context.size() + entry.correlationId.size() + 32U);
    formatted.append(entry.timestamp);
    formatted.append(" [");
    formatted.append(logLevelName(entry.level));
    formatted.append("] [");
    formatted.append(entry.component.empty() ? "app" : entry.component);
    formatted.append("] ");
    if (!entry.correlationId.empty()) {
        formatted.append("id=");
        formatted.append(entry.correlationId);
        formatted.push_back(' ');
    }
    formatted.append(entry.message);
    if (!entry.context.empty()) {
        formatted.append(" | ");
        formatted.append(entry.context);
    }
    return formatted;
}

ConsoleLogSink::ConsoleLogSink(std::ostream& output)
    : output_(output)
{
}

util::Result<void> ConsoleLogSink::write(std::string_view line, LogLevel level)
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        output_ << line << '\n';
        if (level == LogLevel::Fatal || level == LogLevel::Error) {
            output_.flush();
        }
        if (!output_) {
            return ioFailure("Falha ao escrever no console.");
        }
        return util::Result<void>::success();
    } catch (...) {
        return ioFailure("Falha inesperada ao escrever no console.");
    }
}

util::Result<void> ConsoleLogSink::flush()
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        output_.flush();
        return output_ ? util::Result<void>::success()
                       : ioFailure("Falha ao descarregar o console.");
    } catch (...) {
        return ioFailure("Falha inesperada ao descarregar o console.");
    }
}

FileLogSink::FileLogSink(std::filesystem::path directory, std::uintmax_t maxFileBytes)
    : directory_(std::move(directory)),
      maxFileBytes_(std::max<std::uintmax_t>(1U, maxFileBytes))
{
}

util::Result<std::shared_ptr<FileLogSink>> FileLogSink::create(
    std::filesystem::path directory,
    std::uintmax_t maxFileBytes)
{
    try {
        auto sink = std::shared_ptr<FileLogSink>(
            new FileLogSink(std::move(directory), maxFileBytes));
        std::lock_guard<std::mutex> lock(sink->mutex_);
        const auto opened = sink->ensureOpenLocked();
        if (!opened) {
            return util::Result<std::shared_ptr<FileLogSink>>::failure(opened.error());
        }
        return util::Result<std::shared_ptr<FileLogSink>>::success(std::move(sink));
    } catch (const std::exception&) {
        return util::Result<std::shared_ptr<FileLogSink>>::failure(
            util::ErrorCode::Io,
            "Não foi possível inicializar o arquivo de log.",
            "Verifique o diretório e as permissões de escrita.");
    } catch (...) {
        return util::Result<std::shared_ptr<FileLogSink>>::failure(
            util::ErrorCode::Io, "Falha desconhecida ao inicializar o arquivo de log.");
    }
}

std::string FileLogSink::currentDate()
{
    const std::string timestamp = util::iso8601Now();
    if (timestamp.size() < 10U) {
        return "unknown-date";
    }
    std::string date = timestamp.substr(0, 10);
    date.erase(std::remove(date.begin(), date.end(), '-'), date.end());
    return date;
}

util::Result<void> FileLogSink::ensureOpenLocked()
{
    const std::string date = currentDate();
    if (stream_.is_open() && openDate_ == date) {
        return util::Result<void>::success();
    }

    if (stream_.is_open()) {
        stream_.flush();
        stream_.close();
    }

    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) {
        return ioFailure("Não foi possível criar o diretório de logs.",
                         "Verifique o caminho relativo e as permissões.");
    }

    openDate_ = date;
    currentPath_ = directory_ / ("polphone-" + date + ".log");
    stream_.open(currentPath_, std::ios::out | std::ios::app | std::ios::binary);
    if (!stream_.is_open()) {
        return ioFailure("Não foi possível abrir o arquivo de log.",
                         "O console continuará disponível.");
    }
    return util::Result<void>::success();
}

util::Result<void> FileLogSink::rotateLocked()
{
    stream_.flush();
    stream_.close();

    const std::filesystem::path firstBackup = currentPath_.string() + ".1";
    const std::filesystem::path secondBackup = currentPath_.string() + ".2";
    std::error_code error;
    std::filesystem::remove(secondBackup, error);
    error.clear();
    if (std::filesystem::exists(firstBackup, error)) {
        error.clear();
        std::filesystem::rename(firstBackup, secondBackup, error);
        if (error) {
            return ioFailure("Não foi possível rotacionar o arquivo de log.");
        }
    }
    error.clear();
    std::filesystem::rename(currentPath_, firstBackup, error);
    if (error) {
        return ioFailure("Não foi possível rotacionar o arquivo de log.");
    }

    stream_.open(currentPath_, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!stream_.is_open()) {
        return ioFailure("Não foi possível reabrir o arquivo de log após a rotação.");
    }
    return util::Result<void>::success();
}

util::Result<void> FileLogSink::write(std::string_view line, LogLevel level)
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto opened = ensureOpenLocked();
        if (!opened) {
            return opened;
        }

        std::error_code error;
        const std::uintmax_t currentSize = std::filesystem::file_size(currentPath_, error);
        if (!error && currentSize + line.size() + 1U > maxFileBytes_) {
            const auto rotated = rotateLocked();
            if (!rotated) {
                return rotated;
            }
        }

        stream_ << line << '\n';
        if (level == LogLevel::Fatal || level == LogLevel::Error) {
            stream_.flush();
        }
        if (!stream_) {
            return ioFailure("Falha ao escrever no arquivo de log.");
        }
        return util::Result<void>::success();
    } catch (...) {
        return ioFailure("Falha inesperada durante a escrita do arquivo de log.");
    }
}

util::Result<void> FileLogSink::flush()
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stream_.is_open()) {
            stream_.flush();
            if (!stream_) {
                return ioFailure("Falha ao descarregar o arquivo de log.");
            }
        }
        return util::Result<void>::success();
    } catch (...) {
        return ioFailure("Falha inesperada ao descarregar o arquivo de log.");
    }
}

std::filesystem::path FileLogSink::currentPath() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return currentPath_;
}

Logger::~Logger()
{
    static_cast<void>(flush());
    std::lock_guard<std::mutex> lock(sinksMutex_);
    sinks_.clear();
}

bool Logger::addSink(std::shared_ptr<LogSink> sink, LogLevel maxLevel) noexcept
{
    if (!sink) {
        return false;
    }
    try {
        std::lock_guard<std::mutex> lock(sinksMutex_);
        sinks_.push_back(SinkRegistration{std::move(sink), maxLevel});
        return true;
    } catch (...) {
        return false;
    }
}

bool Logger::setSinkLevel(
    const std::shared_ptr<LogSink>& sink,
    LogLevel maxLevel) noexcept
{
    if (!sink) return false;
    try {
        std::lock_guard<std::mutex> lock(sinksMutex_);
        for (auto& registration : sinks_) {
            if (registration.sink == sink) {
                registration.maxLevel = maxLevel;
                return true;
            }
        }
    } catch (...) {
    }
    return false;
}

void Logger::setLogDtmfDigits(bool enabled) noexcept
{
    logDtmfDigits_ = enabled;
}

util::Result<std::filesystem::path> Logger::enableFile(
    const std::filesystem::path& directory,
    LogLevel maxLevel,
    std::uintmax_t maxFileBytes)
{
    const auto created = FileLogSink::create(directory, maxFileBytes);
    if (!created) {
        return util::Result<std::filesystem::path>::failure(created.error());
    }
    const auto sink = created.value();
    if (!addSink(sink, maxLevel)) {
        return util::Result<std::filesystem::path>::failure(
            util::ErrorCode::Runtime, "Não foi possível registrar o destino de arquivo.");
    }
    return util::Result<std::filesystem::path>::success(sink->currentPath());
}

std::vector<Logger::SinkRegistration> Logger::sinksSnapshot() const
{
    std::lock_guard<std::mutex> lock(sinksMutex_);
    return sinks_;
}

bool Logger::log(LogLevel level,
                 std::string_view component,
                 std::string_view message,
                 std::string_view context,
                 std::string_view correlationId) noexcept
{
    ReentryGuard guard(this);
    if (!guard.entered() || level == LogLevel::Off) {
        return false;
    }

    try {
        LogEntry entry;
        entry.timestamp = util::iso8601Now();
        entry.level = level;
        const bool logDtmfDigits = logDtmfDigits_.load();
        entry.component = Redactor::redact(component, logDtmfDigits);
        entry.message = Redactor::redact(message, logDtmfDigits);
        entry.context = Redactor::redact(context, logDtmfDigits);
        entry.correlationId = Redactor::redact(correlationId, logDtmfDigits);
        const std::string formatted = formatLogEntry(entry);

        bool success = true;
        for (const auto& registration : sinksSnapshot()) {
            if (!accepts(level, registration.maxLevel)) {
                continue;
            }
            try {
                const auto written = registration.sink->write(formatted, level);
                success = static_cast<bool>(written) && success;
            } catch (...) {
                success = false;
            }
        }
        return success;
    } catch (...) {
        return false;
    }
}

bool Logger::fatal(std::string_view component, std::string_view message) noexcept
{
    return log(LogLevel::Fatal, component, message);
}

bool Logger::error(std::string_view component, std::string_view message) noexcept
{
    return log(LogLevel::Error, component, message);
}

bool Logger::warning(std::string_view component, std::string_view message) noexcept
{
    return log(LogLevel::Warning, component, message);
}

bool Logger::info(std::string_view component, std::string_view message) noexcept
{
    return log(LogLevel::Info, component, message);
}

bool Logger::debug(std::string_view component, std::string_view message) noexcept
{
    return log(LogLevel::Debug, component, message);
}

bool Logger::trace(std::string_view component, std::string_view message) noexcept
{
    return log(LogLevel::Trace, component, message);
}

bool Logger::flush() noexcept
{
    bool success = true;
    try {
        for (const auto& registration : sinksSnapshot()) {
            try {
                const auto flushed = registration.sink->flush();
                success = static_cast<bool>(flushed) && success;
            } catch (...) {
                success = false;
            }
        }
    } catch (...) {
        return false;
    }
    return success;
}

} // namespace polphone::logging
