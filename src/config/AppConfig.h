/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include <map>
#include <string>

namespace polphone::config {

struct SipConfig {
    std::string idUri;
    std::string registrarUri;
    std::string realm{"*"};
    std::string username;
    std::string password;
    std::string domain;
    std::string proxyUri;
    int regTimeoutSec{300};
    int regRetryIntervalSec{10};
    bool registerOnStartup{true};
};

struct NetworkConfig {
    int localPort{0};
    std::string boundAddress;
    std::string transport{"udp"};
};

struct AudioConfig {
    std::string captureDevice;
    std::string playbackDevice;
    int clockRate{8000};
    int channelCount{1};
    int ptimeMs{20};
    int ecTailMs{200};
    int quality{8};
    bool noVad{true};
};

struct CodecsConfig {
    std::map<std::string, int> priority{
        {"PCMU/8000/1", 254},
        {"PCMA/8000/1", 253},
        {"G722/16000/1", 100},
        {"speex/8000/1", 0},
        {"iLBC/8000/1", 0},
        {"GSM/8000/1", 0},
    };
};

struct DtmfConfig {
    std::string defaultMethod{"rfc4733"};
    int durationMs{160};
    int gapMs{100};
    int volumeDbm0{-10};
    bool localFeedback{false};
    bool logDigits{false};
};

struct LoggingConfig {
    int consoleLevel{4};
    int fileLevel{5};
    std::string directory{"logs"};
    int maxFileMB{50};
    bool sipMessageTrace{true};
};

struct AppConfig {
    SipConfig sip;
    NetworkConfig network;
    AudioConfig audio;
    CodecsConfig codecs;
    DtmfConfig dtmf;
    LoggingConfig logging;

    [[nodiscard]] std::string redactedDump() const;
};

} // namespace polphone::config
