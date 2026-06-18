#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>

using namespace okay;

int main() {
    RUN_SUITE("prefs");

    Prefs::Clear();

    // --- Set / get across types ---
    Prefs::SetInt("score", 1234);
    Prefs::SetFloat("volume", 0.75f);
    Prefs::SetString("name", "Player One");
    CHECK(Prefs::GetInt("score") == 1234);
    CHECK_NEAR(Prefs::GetFloat("volume"), 0.75f, 0.0001f);
    CHECK(Prefs::GetString("name") == "Player One");

    // --- Defaults for missing keys ---
    CHECK(Prefs::GetInt("missing", 42) == 42);
    CHECK(Prefs::GetString("missing", "n/a") == "n/a");
    CHECK(!Prefs::Has("missing"));
    CHECK(Prefs::Has("score"));

    // --- Delete ---
    Prefs::Delete("score");
    CHECK(!Prefs::Has("score"));

    // --- Save / Load round-trip (including a value with special chars) ---
    Prefs::SetString("greeting", "line1\nline2\twith\\slash");
    const char* path = "test_prefs.tmp";
    CHECK(Prefs::Save(path));

    Prefs::Clear();
    CHECK(!Prefs::Has("name"));
    CHECK(Prefs::Load(path));
    CHECK(Prefs::GetString("name") == "Player One");
    CHECK_NEAR(Prefs::GetFloat("volume"), 0.75f, 0.0001f);
    CHECK(Prefs::GetString("greeting") == "line1\nline2\twith\\slash");

    std::remove(path);

    // --- Loading a missing file is a no-op failure, not a crash ---
    CHECK(!Prefs::Load("definitely_not_here.okayprefs"));

    TEST_MAIN_RESULT();
}
