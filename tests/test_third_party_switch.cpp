// The master switch back to the third-party code (engine/include/okay/Core/ThirdPartySwitch.hpp), the own defaults the
// programs apply at start-up and their start-up line (shared/OwnStack.hpp), and the fallback test hook OKAY_TEST_OWN_FAIL.
// Owner decision 2026-11-17: our own replacements are the default; OKAY_THIRD_PARTY=1 (or each feature's own variable) goes back.
//
// Two ctest entries because okay::platform decides its default on first use and keeps it:
//   test_third_party_switch            the switch words, WAV, the summary line, the fallback hook, and that ApplyOwnDefaults makes
//                                      our own platform backend the default (when it is compiled in)
//   test_third_party_switch forced     with OKAY_THIRD_PARTY=1 set before anything runs: ApplyOwnDefaults sets nothing and the
//                                      platform default is SDL (when SDL is compiled in); FallBackTo answers as documented
#include "test_framework.hpp"
#include "OwnStack.hpp"
#include "okay/Audio/WavDecoder.hpp"
#include "okay/Core/ThirdPartySwitch.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

namespace pl = okay::platform;

static void Unset(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}
static void Set(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

static void Words() {
    RUN_SUITE("OKAY_THIRD_PARTY: the words");
    Unset("OKAY_THIRD_PARTY");
    CHECK(!okay::ThirdPartyForced());
    for (const char* on : {"1", "on", "ON", "yes", "True"}) { Set("OKAY_THIRD_PARTY", on); CHECK(okay::ThirdPartyForced()); }
    for (const char* off : {"0", "off", "no", "2", "onn", "truetruetrue", " 1"}) { Set("OKAY_THIRD_PARTY", off); CHECK(!okay::ThirdPartyForced()); }
    Unset("OKAY_THIRD_PARTY");
}

static void FailHook() {
    RUN_SUITE("OKAY_TEST_OWN_FAIL: the fallback test hook");
    Unset("OKAY_TEST_OWN_FAIL");
    CHECK(!okay::OwnStartForcedToFail("window"));
    Set("OKAY_TEST_OWN_FAIL", "1");
    CHECK(okay::OwnStartForcedToFail("window") && okay::OwnStartForcedToFail("renderer") && okay::OwnStartForcedToFail("platform"));
    Set("OKAY_TEST_OWN_FAIL", "all");
    CHECK(okay::OwnStartForcedToFail("renderer"));
    Set("OKAY_TEST_OWN_FAIL", "renderer,platform");
    CHECK(!okay::OwnStartForcedToFail("window") && okay::OwnStartForcedToFail("renderer") && okay::OwnStartForcedToFail("platform"));
    Set("OKAY_TEST_OWN_FAIL", "win");
    CHECK(!okay::OwnStartForcedToFail("window"));   // whole words only
    CHECK(!okay::OwnStartForcedToFail(nullptr));
    Unset("OKAY_TEST_OWN_FAIL");
}

static void Wav() {
    RUN_SUITE("WAV follows OKAY_THIRD_PARTY; OKAY_OWN_WAV still wins");
    Unset("OKAY_OWN_WAV"); Unset("OKAY_THIRD_PARTY");
#ifndef OKAY_OWN_WAV_DEFAULT_OFF
    CHECK(okay::wav::OwnWavEnabled());
#endif
    Set("OKAY_THIRD_PARTY", "1");
    CHECK(!okay::wav::OwnWavEnabled());
    Set("OKAY_OWN_WAV", "1");
    CHECK(okay::wav::OwnWavEnabled());
    Unset("OKAY_THIRD_PARTY"); Set("OKAY_OWN_WAV", "0");
    CHECK(!okay::wav::OwnWavEnabled());
    Unset("OKAY_OWN_WAV");
}

static void Line() {
    RUN_SUITE("the start-up line");
    using okay::ownstack::Part;
    Unset("OKAY_THIRD_PARTY");
    const std::string all = okay::ownstack::SummaryLine({{"platform", true}, {"renderer", true}, {"wav", true}});
    CHECK(all == "own: platform, renderer, wav; third party: libsodium (set OKAY_THIRD_PARTY=1 for all old paths)");
    const std::string mixed = okay::ownstack::SummaryLine({{"platform", false}, {"wav", true}, {"fonts", false}});
    CHECK(mixed == "own: wav; third party: platform, fonts, libsodium (set OKAY_THIRD_PARTY=1 for all old paths)");
    Set("OKAY_THIRD_PARTY", "1");
    const std::string none = okay::ownstack::SummaryLine({{"platform", false}});
    CHECK(none == "own: nothing; third party: platform, libsodium (OKAY_THIRD_PARTY=1 is set: old paths)");
    Unset("OKAY_THIRD_PARTY");
}

int main(int argc, char** argv) {
    const bool forced = argc > 1 && std::strcmp(argv[1], "forced") == 0;
    if (forced) {
        RUN_SUITE("OKAY_THIRD_PARTY=1 at start-up");
        Set("OKAY_THIRD_PARTY", "1");
        Unset("OKAY_PLATFORM_BACKEND");
        CHECK(okay::ownstack::ApplyOwnDefaults() == 0);
        CHECK(std::getenv("OKAY_PLATFORM_BACKEND") == nullptr);
        if (pl::IsCompiledIn(pl::BackendKind::Sdl2)) CHECK(pl::DefaultKind() == pl::BackendKind::Sdl2);
        CHECK(pl::FallBackTo(pl::BackendKind::Sdl2) == pl::IsCompiledIn(pl::BackendKind::Sdl2));
        Unset("OKAY_THIRD_PARTY");
        TEST_MAIN_RESULT();
    }
    Words();
    FailHook();
    Wav();
    Line();
    RUN_SUITE("ApplyOwnDefaults: our own platform backend is the programs' default");
    Unset("OKAY_THIRD_PARTY");
    Unset("OKAY_PLATFORM_BACKEND");
    const int n = okay::ownstack::ApplyOwnDefaults();
    if (pl::IsCompiledIn(pl::BackendKind::Own)) {
        CHECK(n == 1);
        const char* v = std::getenv("OKAY_PLATFORM_BACKEND");
        CHECK(v && std::strcmp(v, "own") == 0);
        CHECK(pl::DefaultKind() == pl::BackendKind::Own);
        CHECK(okay::ownstack::ApplyOwnDefaults() == 0);   // already set: left alone
        // The automatic fallback: after FallBackTo(Sdl2) the default answers SDL (when it is compiled in).
        if (pl::IsCompiledIn(pl::BackendKind::Sdl2)) {
            CHECK(pl::FallBackTo(pl::BackendKind::Sdl2));
            CHECK(pl::DefaultKind() == pl::BackendKind::Sdl2);
        }
    } else {
        CHECK(n == 0);
    }
    TEST_MAIN_RESULT();
}
