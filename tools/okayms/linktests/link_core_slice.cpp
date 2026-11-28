// Link-and-run test of a bigger slice of the engine (Round 36): engine/src/Core/CommandLine.cpp (the command-line
// parser every program uses) and engine/src/Core/StringId.cpp (interned names, a table behind a CRITICAL_SECTION
// lock), compiled by okay-cl into objects of their own and linked with this test. It drives both the way the engine
// does (declare options, parse, read values; intern names, count, detect a collision) and prints what it saw. Built by
// okay-cl (/O0 /O1 /O2) and by MinGW g++, run under Wine; the exit code and the printed text must be the same.
#include "mini.hpp"
#include "okay/Core/CommandLine.hpp"
#include "okay/Core/StringId.hpp"
#include <string>
#include <vector>

using namespace okay;

static int g_collisions = 0;
static void OnCollision(std::string_view, std::string_view, StringId::Value) { ++g_collisions; }

int main() {
    RUN_SUITE("core_slice");
    CommandLine cl;
    cl.Flag("verbose", "print more").Short('v');
    cl.Value("width", "N", "window width", "1280");
    cl.Value("name", "TEXT", "player name");
    cl.OptionalValue("log", "FILE", "log file");
    const char* argv[] = {"game", "-v", "--width=1920", "--name", "Ada", "--log", "level1.okayscene", "extra"};
    CHECK(cl.Parse(8, argv));
    CHECK(cl.Error().empty());
    CHECK(cl.Has("verbose") && cl.GetBool("verbose"));
    CHECK(cl.GetInt("width") == 1920);
    CHECK(cl.Get("name") == "Ada");
    int width = 0;
    CHECK(cl.TryGetInt("width", 640, 4096, width) && width == 1920);
    CHECK(!cl.TryGetInt("width", 640, 1000, width));
    std::printf("width %d name %s log '%s' positionals %d\n", cl.GetInt("width"), cl.Get("name").c_str(),
                cl.Get("log").c_str(), (int)cl.Positionals().size());
    CommandLine bad;
    bad.Flag("x");
    const std::vector<std::string> wrong{"--nope"};
    CHECK(!bad.Parse(wrong));
    std::printf("error: %s\n", bad.Error().c_str());
    CommandLine defaults;
    defaults.Value("width", "N", "", "1280");
    CHECK(defaults.Parse(std::vector<std::string>{}));
    CHECK(defaults.GetInt("width") == 1280);

    StringId::ClearTable();
    StringId::SetCollisionCheck(true);
    StringId::SetCollisionHandler(&OnCollision);
    const StringId a = StringId::Intern("Player");
    const StringId b = StringId::Intern("Enemy");
    const StringId a2 = StringId::Intern("Player");
    CHECK(a == a2 && a != b && a == StringId::FromString("Player"));
    CHECK(a.Name() == "Player" && b.IsInterned());
    CHECK(StringId::InternedCount() == 2);
    const StringId c1 = StringId::InternWithHash(42, "first");
    const StringId c2 = StringId::InternWithHash(42, "second");
    CHECK(c1 == c2 && c1.Name() == "first");
    CHECK(StringId::CollisionCount() == 1 && g_collisions == 1);
    std::printf("interned %d collisions %d hash %u name %s\n", (int)StringId::InternedCount(),
                (int)StringId::CollisionCount(), (unsigned)a.Hash(), a.Name().c_str());
    TEST_MAIN_RESULT();
}
