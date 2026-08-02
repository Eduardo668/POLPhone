/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "audio/AudioDeviceService.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace {

using polphone::audio::AudioDeviceDescription;
using polphone::audio::AudioDeviceDirection;
using polphone::audio::AudioDeviceService;

const std::vector<AudioDeviceDescription> kDevices{
    {0, "Wave Mapper", 2, 2, 44100, "WMME"},
    {1, "Grupo de Microfones USB muito l", 2, 0, 48000, "WMME"},
    {2, "Alto-falantes Realtek", 0, 2, 48000, "WMME"},
    {3, "Microfone secundário", 1, 0, 48000, "WMME"},
};

} // namespace

TEST_SUITE("audio-devices") {
    TEST_CASE("classifica dispositivos por direção")
    {
        CHECK(kDevices[0].supports(AudioDeviceDirection::Capture));
        CHECK(kDevices[0].supports(AudioDeviceDirection::Playback));
        CHECK(kDevices[1].supports(AudioDeviceDirection::Capture));
        CHECK_FALSE(kDevices[1].supports(AudioDeviceDirection::Playback));
        CHECK(kDevices[2].supports(AudioDeviceDirection::Playback));
        CHECK_FALSE(kDevices[2].supports(AudioDeviceDirection::Capture));
    }

    TEST_CASE("resolve nome parcial sem diferenciar caixa")
    {
        const auto capture = AudioDeviceService::resolveByName(
            kDevices, "MICROFONES USB", AudioDeviceDirection::Capture);
        REQUIRE(capture);
        CHECK(capture.value() == 1);

        const auto playback = AudioDeviceService::resolveByName(
            kDevices, "realtek", AudioDeviceDirection::Playback);
        REQUIRE(playback);
        CHECK(playback.value() == 2);
    }

    TEST_CASE("aceita nome completo quando WMME devolveu 31 caracteres")
    {
        const auto result = AudioDeviceService::resolveByName(
            kDevices,
            "Grupo de Microfones USB muito longo e estável",
            AudioDeviceDirection::Capture);
        REQUIRE(result);
        CHECK(result.value() == 1);
    }

    TEST_CASE("recusa nome ausente, ambíguo ou direção incompatível")
    {
        CHECK_FALSE(AudioDeviceService::resolveByName(
            kDevices, "inexistente", AudioDeviceDirection::Capture));
        CHECK_FALSE(AudioDeviceService::resolveByName(
            kDevices, "micro", AudioDeviceDirection::Capture));
        CHECK_FALSE(AudioDeviceService::resolveByName(
            kDevices, "realtek", AudioDeviceDirection::Capture));
    }
}
