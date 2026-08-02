/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include "util/Result.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace polphone::logging {

enum class LogLevel : unsigned {
    Off = 0,
    Fatal = 1,
    Error = 2,
    Warning = 3,
    Info = 4,
    Debug = 5,
    Trace = 6
};

struct LogEntry {
    std::string timestamp;
    LogLevel level{LogLevel::Info};
    std::string component;
    std::string message;
    std::string context;
    std::string correlationId;
};

[[nodiscard]] LogLevel logLevelFromNumber(int level) noexcept;
[[nodiscard]] unsigned logLevelNumber(LogLevel level) noexcept;
[[nodiscard]] std::string_view logLevelName(LogLevel level) noexcept;
[[nodiscard]] std::string formatLogEntry(const LogEntry& entry);

class LogSink {
public:
    virtual ~LogSink() = default;
    virtual util::Result<void> write(std::string_view line, LogLevel level) = 0;
    virtual util::Result<void> flush() = 0;
};

class ConsoleLogSink final : public LogSink {
public:
    explicit ConsoleLogSink(std::ostream& output);

    util::Result<void> write(std::string_view line, LogLevel level) override;
    util::Result<void> flush() override;

private:
    std::ostream& output_;
    std::mutex mutex_;
};

class FileLogSink final : public LogSink {
public:
    static util::Result<std::shared_ptr<FileLogSink>> create(
        std::filesystem::path directory,
        std::uintmax_t maxFileBytes = 50U * 1024U * 1024U);

    util::Result<void> write(std::string_view line, LogLevel level) override;
    util::Result<void> flush() override;

    [[nodiscard]] std::filesystem::path currentPath() const;

private:
    FileLogSink(std::filesystem::path directory, std::uintmax_t maxFileBytes);

    util::Result<void> ensureOpenLocked();
    util::Result<void> rotateLocked();
    [[nodiscard]] static std::string currentDate();

    std::filesystem::path directory_;
    std::uintmax_t maxFileBytes_;
    std::string openDate_;
    std::filesystem::path currentPath_;
    std::ofstream stream_;
    mutable std::mutex mutex_;
};

class Logger final {
public:
    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    [[nodiscard]] bool addSink(std::shared_ptr<LogSink> sink, LogLevel maxLevel) noexcept;
    [[nodiscard]] bool setSinkLevel(const std::shared_ptr<LogSink>& sink,
                                    LogLevel maxLevel) noexcept;
    void setLogDtmfDigits(bool enabled) noexcept;
    util::Result<std::filesystem::path> enableFile(
        const std::filesystem::path& directory,
        LogLevel maxLevel,
        std::uintmax_t maxFileBytes = 50U * 1024U * 1024U);

    [[nodiscard]] bool log(LogLevel level,
                           std::string_view component,
                           std::string_view message,
                           std::string_view context = {},
                           std::string_view correlationId = {}) noexcept;

    [[nodiscard]] bool fatal(std::string_view component, std::string_view message) noexcept;
    [[nodiscard]] bool error(std::string_view component, std::string_view message) noexcept;
    [[nodiscard]] bool warning(std::string_view component, std::string_view message) noexcept;
    [[nodiscard]] bool info(std::string_view component, std::string_view message) noexcept;
    [[nodiscard]] bool debug(std::string_view component, std::string_view message) noexcept;
    [[nodiscard]] bool trace(std::string_view component, std::string_view message) noexcept;
    [[nodiscard]] bool flush() noexcept;

private:
    struct SinkRegistration {
        std::shared_ptr<LogSink> sink;
        LogLevel maxLevel;
    };

    [[nodiscard]] std::vector<SinkRegistration> sinksSnapshot() const;

    mutable std::mutex sinksMutex_;
    std::vector<SinkRegistration> sinks_;
    std::atomic<bool> logDtmfDigits_{false};
};

} // namespace polphone::logging
