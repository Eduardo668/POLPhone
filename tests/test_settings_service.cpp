/* POLPhone - testes de persistência segura das configurações GUI. GPL-2.0-only. */

#include "config/ConfigLoader.h"
#include "core/SettingsService.h"

#include <doctest/doctest.h>

#include <filesystem>

using namespace polphone;

TEST_SUITE("settings-service") {
    TEST_CASE("salva identidade separada e recarrega sem devolver senha à GUI")
    {
        const auto path = std::filesystem::temp_directory_path()
            / "polphone-settings-service-test.json";
        std::error_code ignored;
        std::filesystem::remove(path, ignored);

        core::GuiSettings input;
        input.registrarUri = "sip:pbx.invalid:5060";
        input.idUri = "sip:600@pbx.invalid";
        input.displayName = "Operador POL";
        input.username = "600";
        input.authUsername = "digest-600";
        input.password = "ONLY_SYNTHETIC_TEST_SECRET";
        input.domain = "pbx.invalid";
        input.ringtoneEnabled = false;
        input.topmostOnIncomingCall = true;
        input.dtmfMethod = "inband";
        input.dtmfDurationMs = 280;
        input.dtmfGapMs = 120;
        input.inbandVolumeDbm0 = -6;

        REQUIRE(core::saveGuiSettings(path, input));
        const auto raw = config::ConfigLoader::load(path);
        REQUIRE(raw);
        CHECK(raw.value().sip.displayName == "Operador POL");
        CHECK(raw.value().sip.username == "600");
        CHECK(raw.value().sip.authUsername == "digest-600");
        CHECK(raw.value().sip.password == "ONLY_SYNTHETIC_TEST_SECRET");
        CHECK(raw.value().dtmf.defaultMethod == "inband");
        CHECK(raw.value().dtmf.durationMs == 280);
        CHECK(raw.value().dtmf.gapMs == 120);
        CHECK(raw.value().dtmf.volumeDbm0 == -6);

        const auto loaded = core::loadGuiSettings(path);
        std::filesystem::remove(path, ignored);
        REQUIRE(loaded);
        CHECK(loaded.value().displayName == "Operador POL");
        CHECK(loaded.value().username == "600");
        CHECK(loaded.value().authUsername == "digest-600");
        CHECK(loaded.value().password.empty());
        CHECK(loaded.value().dtmfMethod == "inband");
        CHECK(loaded.value().dtmfDurationMs == 280);
        CHECK(loaded.value().dtmfGapMs == 120);
        CHECK(loaded.value().inbandVolumeDbm0 == -6);
        CHECK_FALSE(loaded.value().ringtoneEnabled);
        CHECK(loaded.value().topmostOnIncomingCall);
    }
}
