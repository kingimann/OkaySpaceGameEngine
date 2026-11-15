// A stray number in a scene file (Grok, issue #888 finding 3). examples/spatial_audio had "gravity 0 -9.81 0": the 2D
// gravity record reads two numbers, so the third became a top-level "unknown record" named "0", was kept, and was
// written back by every save. A record or field name starts with a letter now, so a stray number is dropped (with a
// warning), while a real record from a newer build is still kept. The examples are fixed too. Written for this engine.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <string>

using namespace okay;

static bool HasLine(const std::string& text, const std::string& line) {
    return text.find("\n" + line + "\n") != std::string::npos || text.rfind(line + "\n", 0) == 0;
}

static void TestTopLevel() {
    RUN_SUITE("stray number: a top-level number is not kept as a record");
    const std::string text =
        "okayscene 3\nname \"Stray\"\ngravity 0 -9.81 0\nnewthing 1 2\ngameobject 0 \"A\"\n  active 1\n  parent -1\n"
        "  transform 0 0 0 0 0 0 1 1 1 1\nend\n";
    Scene s("s");
    std::string err;
    CHECK(SceneSerializer::Deserialize(s, text, &err));
    bool sawZero = false, sawNew = false;
    for (const std::string& r : s.unknownRecords) { if (r.rfind("0", 0) == 0) sawZero = true; if (r.rfind("newthing", 0) == 0) sawNew = true; }
    CHECK(!sawZero);                                                     // the stray 0 is dropped
    CHECK(sawNew);                                                       // a newer build's record is still kept
    const std::string again = SceneSerializer::Serialize(s);
    CHECK(!HasLine(again, "0"));
    CHECK(HasLine(again, "newthing 1 2"));
}

static void TestObjectLevel() {
    RUN_SUITE("stray number: an object-level number is not kept as a field");
    const std::string text =
        "okayscene 3\nname \"Stray\"\ngameobject 0 \"A\"\n  active 1 7\n  newfield x\n  parent -1\n"
        "  transform 0 0 0 0 0 0 1 1 1 1\nend\n";
    Scene s("s");
    std::string err;
    CHECK(SceneSerializer::Deserialize(s, text, &err));
    const auto roots = s.Roots();
    CHECK(roots.size() == 1);
    if (roots.empty()) return;
    bool sawSeven = false, sawField = false;
    for (const std::string& r : roots[0]->unknownRecords) { if (r.rfind("7", 0) == 0) sawSeven = true; if (r.rfind("newfield", 0) == 0) sawField = true; }
    CHECK(!sawSeven);
    CHECK(sawField);
}

static void TestExamples() {
    RUN_SUITE("stray number: the shipped examples load with no unknown records and re-save stably");
    for (const char* rel : {"/examples/spatial_audio/spatial_audio.okayscene", "/examples/own_wav/own_wav_test.okayscene"}) {
        Scene s("ex");
        std::string err;
        CHECK(SceneSerializer::LoadFromFile(s, std::string(OKAY_SOURCE_DIR) + rel, &err));
        CHECK(s.unknownRecords.empty());
        for (const auto& up : s.Objects()) CHECK(up->unknownRecords.empty());
        const std::string a = SceneSerializer::Serialize(s);
        Scene t("t");
        CHECK(SceneSerializer::Deserialize(t, a, &err));
        CHECK(SceneSerializer::Serialize(t) == a);
    }
}

int main() {
    TestTopLevel();
    TestObjectLevel();
    TestExamples();
    TEST_MAIN_RESULT();
}
