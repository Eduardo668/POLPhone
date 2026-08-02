/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include "app/CommandParser.h"

#include <atomic>
#include <iosfwd>
#include <string>

namespace polphone::app {

class Application;

class ConsoleUi final {
public:
    ConsoleUi(Application& application,
              std::istream& input,
              std::ostream& output,
              std::ostream& error) noexcept;

    ConsoleUi(const ConsoleUi&) = delete;
    ConsoleUi& operator=(const ConsoleUi&) = delete;

    [[nodiscard]] int run();

private:
    enum class DispatchResult {
        Continue,
        Quit
    };

    void printHelp();
    void printStatus();
    void printDevices();
    void printCodecs();
    void drainEvents();
    void printError(const util::Error& error);
    [[nodiscard]] std::string prompt() const;
    [[nodiscard]] DispatchResult dispatch(const Command& command);

    Application& application_;
    std::istream& input_;
    std::ostream& output_;
    std::ostream& error_;
    std::atomic<bool> interrupted_{false};
};

} // namespace polphone::app
