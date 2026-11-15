// Our own CSM reader (engine/src/Render/CsmReader.cpp). SYNTHETIC fixtures built in memory (tests/batche_fixtures.hpp), checked
// against numbers worked out by hand; the real file (ThomasFechten.csm) is checked by test_batche_hostile when OKAY_MODEL_CORPUS is set.
// Builds on its own with -DOKAY_STANDALONE_TEST (reader source only).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batche_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace okay;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static bool Refused(const std::string& text, const char* why) {
    ImportedModel m; std::string err;
    const bool ok = ReadCsmBuffer(text, m, &err);
    if (ok) { std::fprintf(stderr, "  expected a refusal (%s) but the file was read\n", why); return false; }
    if (!m.parts.empty() || !m.nodes.empty() || !m.clips.empty()) { std::fprintf(stderr, "  refusal left a model behind (%s)\n", why); return false; }
    if (why && err.find(why) == std::string::npos) { std::fprintf(stderr, "  refusal message \"%s\" does not mention \"%s\"\n", err.c_str(), why); return false; }
    return true;
}

int main() {
    RUN_SUITE("csm_reader");
    for (const char* eol : {"\n", "\r\n", "\r"}) {
        ImportedModel m; std::string err;
        CHECK(ReadCsmBuffer(fx::BuildCsm(eol), m, &err) && fx::Sane(m));
        CHECK(m.backend == "own" && m.parts.empty() && m.materials.empty() && m.nodes.size() == 4 && m.clips.size() == 1);
        CHECK(m.nodes[0].name == "CSM" && m.nodes[1].name == "A" && m.nodes[2].name == "B" && m.nodes[3].name == "C");
        for (int i = 1; i < 4; ++i) CHECK(m.nodes[(std::size_t)i].parent == 0);
        // the node position is the first frame's
        CHECK(fx::Near(m.nodes[2].position.x, 10) && fx::Near(m.nodes[3].position.x, 1) && fx::Near(m.nodes[3].position.y, 2) && fx::Near(m.nodes[3].position.z, 3));
        const ImportedClip& c = m.clips[0];
        CHECK(c.name == "csm" && c.channels.size() == 3);
        const float times[3] = {0.0f, 0.02f, 0.06f};   // (frame - 1) / 50 with frames 1, 2 and 4
        const float expect[3][3][3] = {{{0, 0, 0}, {0, 1, 0}, {0, 3, 0}}, {{10, 0, 0}, {10, 1, 0}, {10, 3, 0}}, {{1, 2, 3}, {1, 2, 4}, {1, 2, 6}}};
        for (std::size_t ch = 0; ch < 3; ++ch) {
            CHECK(c.channels[ch].node == m.nodes[ch + 1].name && c.channels[ch].position.size() == 3 && c.channels[ch].scale.empty() && c.channels[ch].rotation.empty());
            for (std::size_t k = 0; k < 3; ++k) {
                const ImportedKeyV3& key = c.channels[ch].position[k];
                CHECK(fx::Near(key.time, times[k], 1e-6f) && fx::Near(key.value.x, expect[ch][k][0]) && fx::Near(key.value.y, expect[ch][k][1]) && fx::Near(key.value.z, expect[ch][k][2]));
            }
        }
        const float h = 0.70710678f;
        CHECK(fx::Near(m.nodes[0].rotation.x, -h) && fx::Near(m.nodes[0].rotation.w, h));
        CHECK(HasNote(m, "3 marker") && HasNote(m, "Tester"));
        CHECK(HasNote(m, "do not match"));   // the header announces frames 1 to 4 (4 frames), the file has 3 lines (frame 3 is missing)
    }
    {   // a header that says 1 to 3 matches the 3 lines, and the note is not there
        ImportedModel m; std::string err;
        std::string t = fx::BuildCsm();
        const std::size_t at = t.find("$LastFrame 4");
        t.replace(at, 12, "$LastFrame 3");
        ImportedModel m2;
        CHECK(ReadCsmBuffer(t, m2, &err) && !HasNote(m2, "do not match"));
    }
    {   // a UTF-16 file with a byte order mark and a UTF-8 one read the same
        const std::string t = fx::BuildCsm();
        std::string u16 = "\xFF\xFE";
        for (char c : t) { u16.push_back(c); u16.push_back('\0'); }
        ImportedModel a, b; std::string err;
        CHECK(ReadCsmBuffer(u16, a, &err) && a.clips.size() == 1 && a.clips[0].channels[0].position.size() == 3);
        CHECK(ReadCsmBuffer("\xEF\xBB\xBF" + t, b, &err) && b.nodes.size() == 4);
    }
    {   // no $Rate: 30 frames per second, with a note; frame lines that do not fit are skipped and counted; numbers must be plain
        ImportedModel m; std::string err;
        const std::string t = "$Order\nA\n$Points\n1 0 0 0\n2 1 1\n3 x 0 0\n4 inf 0 0\n5 0 0 0 9\n7 3 0 0\n6 9 9 9\n";
        CHECK(ReadCsmBuffer(t, m, &err) && fx::Sane(m));
        const auto& k = m.clips[0].channels[0].position;
        CHECK(k.size() == 2 && fx::Near(k[0].time, 0) && fx::Near(k[1].time, 6.0f / 30.0f, 1e-6f) && fx::Near(k[1].value.x, 3));   // frames 1 and 7; line 6 goes backwards
        CHECK(HasNote(m, "30 frames per second") && HasNote(m, "4 frame line(s) skipped") && HasNote(m, "must increase"));
    }
    {   // a marker named like the root: the root is renamed so the clip's node names stay unique
        ImportedModel m; std::string err;
        CHECK(ReadCsmBuffer("$Order\nCSM\n$Points\n1 1 2 3\n", m, &err) && m.nodes[0].name == "CSM_root" && m.nodes[1].name == "CSM" && fx::Sane(m));
    }
    {   // refusals
        CHECK(Refused("", "$Order"));
        CHECK(Refused("$Points\n1 0 0 0\n", "before any $Order"));
        CHECK(Refused("$Order\nA\n", "$Points"));
        CHECK(Refused("$Order\nA\n$Points\n", "no frame line"));
        CHECK(Refused("$Order\nA\n$Points\n1 0 0\n", "no frame line"));
        CHECK(Refused("$Order\nA A\n$Points\n1 0 0 0 0 0 0\n", "twice"));
        CHECK(Refused("$Order\nA\n$Order\nB\n$Points\n1 0 0 0\n", "second $Order"));
        CHECK(Refused("$Order\nA\n$Points\n1 0 0 0\n$Points\n", "second $Points"));
        CHECK(Refused("$Rate 0\n$Order\nA\n$Points\n1 0 0 0\n", "$Rate"));
        CHECK(Refused("$Rate -5\n$Order\nA\n$Points\n1 0 0 0\n", "$Rate"));
        CHECK(Refused("$Rate nan\n$Order\nA\n$Points\n1 0 0 0\n", "$Rate"));
        CHECK(Refused("$Rate 1e99\n$Order\nA\n$Points\n1 0 0 0\n", "$Rate"));
        CHECK(Refused("$Order\n" + std::string(65, 'M') + "\n$Points\n1 0 0 0\n", "longer than"));
        CHECK(Refused(std::string("\xFF\xFE") + "x", "UTF-16"));   // odd byte count
        {   // 1025 markers: over the marker budget
            std::string t = "$Order\n";
            for (int i = 0; i < 1025; ++i) t += "m" + std::to_string(i) + " ";
            t += "\n$Points\n1 0 0 0\n";
            CHECK(Refused(t, "marker budget"));
            std::string ok = "$Order\n";
            for (int i = 0; i < 1024; ++i) ok += "m" + std::to_string(i) + " ";
            ok += "\n$Points\n1";
            for (int i = 0; i < 1024; ++i) ok += " 1 2 3";
            ok += "\n";
            ImportedModel m; std::string err;
            CHECK(ReadCsmBuffer(ok, m, &err) && m.nodes.size() == 1025);   // exactly at the budget
        }
        {   // the key budget: 1000 markers x 2001 frames
            std::string t = "$Order\n";
            for (int i = 0; i < 1000; ++i) t += "m" + std::to_string(i) + " ";
            t += "\n$Points\n";
            for (int f = 1; f <= 2001; ++f) { t += std::to_string(f); for (int i = 0; i < 1000; ++i) t += " 0 0 0"; t += "\n"; }
            CHECK(Refused(t, "key budget"));
        }
        // every truncation of the good file: read or refused, never a crash, nothing left behind
        const std::string good = fx::BuildCsm();
        for (std::size_t n = 0; n < good.size(); ++n) { ImportedModel m; std::string err; const bool ok = ReadCsmBuffer(good.substr(0, n), m, &err); CHECK(ok ? fx::Sane(m) : (!err.empty() && m.parts.empty() && m.nodes.empty())); }
        // a line with a million tokens is skipped without storing it
        { std::string t = "$Order\nA\n$Points\n1 0 0 0\n2"; for (int i = 0; i < 1000000; ++i) t += " 1"; t += "\n3 1 1 1\n"; ImportedModel m; std::string err; CHECK(ReadCsmBuffer(t, m, &err) && m.clips[0].channels[0].position.size() == 2 && HasNote(m, "1 frame line(s) skipped")); }
    }
    TEST_MAIN_RESULT();
}
