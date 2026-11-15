// Our own LWS / MOT reader (engine/src/Render/LwsReader.cpp). SYNTHETIC scenes built in memory (tests/pmxlws_fixtures.hpp: BuildLws has
// four items and BuildMot one envelope set; every number is checked by hand below), read through a fake object loader that records
// the paths it is asked for. Then: parent loops, a scene that names itself or another scene, every path that climbs out of the scene's
// folder, truncated and flipped files. No real .lws / .mot file is in this repository: the reader is unverified against real files
// (and the radians / bank-first conventions are an assumption, docs/own_lws_reader.md). Builds on its own with -DOKAY_STANDALONE_TEST.
#include "test_framework.hpp"
#include "pmxlws_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

using namespace okay;
using fx::Near;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static const ImportedChannel* Chan(const ImportedModel& m, const char* node) {
    if (m.clips.empty()) return nullptr;
    for (const ImportedChannel& c : m.clips[0].channels) if (c.node == node) return &c;
    return nullptr;
}
static bool Refused(const std::string& text, const char* reasonPart, const IrrMeshLoader* l = nullptr) {
    ImportedModel m; std::string err;
    const bool ok = ReadLwsBuffer(text, "dir/", m, &err, l);
    if (ok) { std::fprintf(stderr, "  accepted (wanted a refusal mentioning '%s')\n", reasonPart); return false; }
    if (err.find(reasonPart) == std::string::npos) { std::fprintf(stderr, "  refused with '%s' (wanted '%s')\n", err.c_str(), reasonPart); return false; }
    return m.parts.empty() && m.nodes.empty();
}

int main() {
    RUN_SUITE("lws_reader");
    for (const char* eol : {"\n", "\r\n"}) {
        std::string text = fx::BuildLws();
        if (eol[1]) { std::string t; for (char c : text) { if (c == '\n') t += "\r\n"; else t.push_back(c); } text = t; }
        fx::FakeLoad fake;
        const IrrMeshLoader loader = fake.Fn();
        ImportedModel m; std::string err;
        const bool ok = ReadLwsBuffer(text, "dir/", m, &err, &loader);
        if (!ok) std::fprintf(stderr, "  refused: %s\n", err.c_str());
        CHECK(ok);
        if (!ok) continue;
        CHECK(m.backend == "own" && fx::Sane(m));
        CHECK(fake.paths.size() == 1 && fake.paths[0] == "dir/Objects/Tri.lwo");
        // nodes, parents first: the null "Pivot" was named after Tri in the file but is Tri's parent
        CHECK(m.nodes.size() == 6);
        CHECK(m.nodes[0].name == "LWS" && m.nodes[1].name == "Pivot" && m.nodes[2].name == "Tri" && m.nodes[3].name == "camera0" && m.nodes[4].name == "Sun" && m.nodes[5].name == "lwo");
        CHECK(m.nodes[1].parent == 0 && m.nodes[2].parent == 1 && m.nodes[3].parent == 0 && m.nodes[4].parent == 0 && m.nodes[5].parent == 2);
        // rest pose is the envelope value at time 0, z mirrored
        CHECK(Near(m.nodes[1].position.x, 10) && Near(m.nodes[1].position.y, 0));
        CHECK(Near(m.nodes[2].position.x, 0) && Near(m.nodes[2].position.y, 5) && Near(m.nodes[2].position.z, -1));
        CHECK(Near(m.nodes[2].scale.x, 3) && Near(m.nodes[2].scale.y, 1) && Near(m.nodes[2].scale.z, 1));
        CHECK(Near(m.nodes[2].rotation.w, 1) && Near(m.nodes[2].rotation.y, 0));
        CHECK(Near(m.nodes[3].position.y, 1) && Near(m.nodes[1].scale.x, 1));
        // the object's mesh sits on the object's node
        CHECK(m.parts.size() == 1 && m.parts[0].node == 5 && fx::TriCount(m) == 1);
        // the clip: Tri has position keys at 0 and 1, heading keys (0 and pi/2) and one scale key
        CHECK(m.clips.size() == 1 && m.clips[0].name == "scene" && m.clips[0].channels.size() == 3);
        const ImportedChannel* t = Chan(m, "Tri");
        CHECK(t != nullptr);
        if (t) {
            CHECK(t->position.size() == 2 && Near(t->position[0].time, 0) && Near(t->position[1].time, 1));
            if (t->position.size() == 2) CHECK(Near(t->position[0].value.x, 0) && Near(t->position[1].value.x, 2) && Near(t->position[1].value.y, 5) && Near(t->position[1].value.z, -1));
            CHECK(t->rotation.size() == 2);
            if (t->rotation.size() == 2) {   // heading pi/2 about Y: (0, sin 45, 0, cos 45); the z mirror turns the axis to (0, -1, 0)
                CHECK(Near(t->rotation[0].value.w, 1));
                CHECK(Near(t->rotation[1].value.x, 0) && Near(t->rotation[1].value.y, -0.70710678f) && Near(t->rotation[1].value.z, 0) && Near(t->rotation[1].value.w, 0.70710678f));
            }
            CHECK(t->scale.size() == 1 && Near(t->scale[0].value.x, 3) && Near(t->scale[0].value.y, 1));
        }
        const ImportedChannel* p = Chan(m, "Pivot");
        CHECK(p && p->position.size() == 1 && Near(p->position[0].value.x, 10) && p->rotation.empty() && p->scale.empty());
        CHECK(Chan(m, "camera0") != nullptr && Chan(m, "Sun") == nullptr);
        CHECK(HasNote(m, "scene version 3") && HasNote(m, "24 frames per second") && HasNote(m, "No real LightWave scene was available"));
        CHECK(HasNote(m, "1 envelope(s) outside a motion channel"));   // the light's intensity envelope
    }
    {   // without a loader the objects are empty nodes
        ImportedModel m; std::string err;
        CHECK(ReadLwsBuffer(fx::BuildLws(), "dir/", m, &err) && m.parts.empty() && m.nodes.size() == 5 && HasNote(m, "1 object(s) not loaded"));
    }
    {   // a motion file: one node, one clip; times are the file's own
        ImportedModel m; std::string err;
        const bool ok = ReadLwsBuffer(fx::BuildMot(), "", m, &err);
        if (!ok) std::fprintf(stderr, "  refused: %s\n", err.c_str());
        CHECK(ok && fx::Sane(m) && m.parts.empty());
        CHECK(m.nodes.size() == 2 && m.nodes[0].name == "LWMO" && m.nodes[1].name == "motion" && m.nodes[1].parent == 0);
        CHECK(Near(m.nodes[1].position.x, 1));
        const ImportedChannel* c = Chan(m, "motion");
        CHECK(c && c->position.size() == 3);
        if (c && c->position.size() == 3) {
            CHECK(Near(c->position[0].time, 0) && Near(c->position[1].time, 0.5f) && Near(c->position[2].time, 1));
            CHECK(Near(c->position[1].value.x, 2) && Near(c->position[2].value.x, 4) && Near(c->position[1].value.y, 0) && Near(c->position[1].value.z, 0));
        }
        CHECK(HasNote(m, "motion file version 3"));
    }
    {   // interpolation between keys of different channels, unsorted keys, two keys at one time, a non linear span
        const std::string s = "LWSC\n3\nAddNullObject N\nObjectMotion\nNumChannels 9\n"
            "Channel 0\n{ Envelope\n 3\n Key 4 2 3 0 0 0 0 0 0\n Key 0 0 0 0 0 0 0 0 0\n Key 1 0 3 0 0 0 0 0 0\n Behaviors 2 2\n}\n"   // t=2 -> 4, t=0 -> 0 then 1 (the later wins), span 0 = TCB
            "Channel 1\n{ Envelope\n 2\n Key 10 1 3 0 0 0 0 0 0\n Key 20 3 3 0 0 0 0 0 0\n Behaviors 1 1\n}\n";
        ImportedModel m; std::string err;
        CHECK(ReadLwsBuffer(s, "", m, &err) && fx::Sane(m));
        const ImportedChannel* c = Chan(m, "N");
        CHECK(c && c->position.size() == 4);   // x has keys at 0 and 2, y at 1 and 3: the union is 0, 1, 2, 3
        if (c && c->position.size() == 4) {
            CHECK(Near(c->position[0].value.x, 1) && Near(c->position[0].value.y, 10));              // x at 0: the later key (1); y before its first key: 10
            CHECK(Near(c->position[1].value.x, 2.5f) && Near(c->position[1].value.y, 10));           // x halfway from 1 (t 0) to 4 (t 2)
            CHECK(Near(c->position[2].value.x, 4) && Near(c->position[2].value.y, 15));              // y halfway from 10 (t 1) to 20 (t 3)
            CHECK(Near(c->position[3].value.x, 4) && Near(c->position[3].value.y, 20));              // x after its last key: 4
        }
        CHECK(HasNote(m, "1 key(s) with a TCB, Hermite, Bezier or stepped span") && HasNote(m, "2 envelope(s) with pre / post behaviours"));
    }
    {   // item graph: a loop, an item that is its own parent, a parent that is not there
        const std::string s = "LWSC\n3\nAddNullObject A\nParentItem 10000001\nAddNullObject B\nParentItem 10000000\nAddNullObject C\nParentItem 10000002\nAddNullObject D\nParentItem 99999999\nAddNullObject E\nParentItem 1000000Z\n";
        ImportedModel m; std::string err;
        CHECK(ReadLwsBuffer(s, "", m, &err) && fx::Sane(m) && m.nodes.size() == 6);
        CHECK(HasNote(m, "2 ParentItem loop(s)") && HasNote(m, "1 item(s) with a ParentItem that names no item") && HasNote(m, "1 bad or misplaced ParentItem"));
        const std::string chain = "LWSC\n3\nAddNullObject A\nParentItem 10000001\nAddNullObject B\nParentItem 10000002\nAddNullObject C\n";   // A under B under C, listed child-first
        CHECK(ReadLwsBuffer(chain, "", m, &err) && fx::Sane(m) && m.nodes.size() == 4 && m.nodes[1].name == "C" && m.nodes[2].name == "B" && m.nodes[3].name == "A" && m.nodes[3].parent == 2 && m.nodes[2].parent == 1 && m.nodes[1].parent == 0);
        // two items with the same name get different node names, so a clip channel names one node
        const std::string dup = "LWSC\n3\nAddNullObject Same\nObjectMotion\nNumChannels 9\n" + fx::Chan(0, {{0, 1}}) + "AddNullObject Same\nObjectMotion\nNumChannels 9\n" + fx::Chan(0, {{0, 2}});
        CHECK(ReadLwsBuffer(dup, "", m, &err) && m.nodes[1].name == "Same" && m.nodes[2].name == "Same_1" && Chan(m, "Same") && Chan(m, "Same_1") && Near(Chan(m, "Same_1")->position[0].value.x, 2));
    }
    {   // "a", "a_1", "a": three different node names, and the clip channel of each reaches its own node
        const std::string s = "LWSC\n3\nAddNullObject a\nObjectMotion\nNumChannels 9\n" + fx::Chan(0, {{0, 1}}) + "AddNullObject a_1\nObjectMotion\nNumChannels 9\n" + fx::Chan(0, {{0, 2}}) + "AddNullObject a\nObjectMotion\nNumChannels 9\n" + fx::Chan(0, {{0, 3}});
        ImportedModel m; std::string err;
        CHECK(ReadLwsBuffer(s, "", m, &err) && m.nodes.size() == 4);
        CHECK(m.nodes[1].name == "a" && m.nodes[2].name == "a_1" && m.nodes[3].name == "a_2");
        CHECK(Chan(m, "a") && Chan(m, "a_1") && Chan(m, "a_2") && Near(Chan(m, "a_2")->position[0].value.x, 3) && Near(Chan(m, "a_1")->position[0].value.x, 2));
        // very long names and paths are cut
        ImportedModel m2;
        CHECK(ReadLwsBuffer("LWSC\n3\nAddNullObject " + std::string(60000, 'n') + "\nLoadObject " + std::string(5000, 'p') + "\n", "", m2, &err) && m2.nodes[1].name.size() == 256 && HasNote(m2, "1 object path(s) longer than 4096"));
    }
    // ---- header ----
    {
        CHECK(Refused("", "empty"));
        CHECK(Refused("\n\n  \n", "empty"));
        CHECK(Refused("LWOB\n3\n", "not a LightWave"));
        CHECK(Refused("lwsc\n3\n", "not a LightWave"));
        CHECK(Refused("LWSC\n9\nAddNullObject A\n", "scene version 9"));
        CHECK(Refused("LWSC\n0\nAddNullObject A\n", "scene version 0"));
        CHECK(Refused("LWSC\nabc\nAddNullObject A\n", "bad version"));
        CHECK(Refused("LWSC\n1e15\nAddNullObject A\n", "scene version"));   // not cast to int before the range check
        CHECK(Refused("LWSC\n-1e15\nAddNullObject A\n", "scene version"));
        CHECK(Refused("LWSC 1e15\nAddNullObject A\n", "scene version"));
        CHECK(Refused("LWSC\n3.5\nAddNullObject A\n", "bad version"));
        CHECK(Refused("LWSC\n3\n", "no object, camera, light or bone"));
        { ImportedModel m; std::string err; CHECK(!ReadLwsBuffer("LWSC", "", m, &err)); }
        // a byte order mark and UTF-16 text are accepted
        ImportedModel m; std::string err;
        CHECK(ReadLwsBuffer("\xEF\xBB\xBFLWSC\n3\nAddNullObject A\n", "", m, &err) && m.nodes.size() == 2);
        std::string u16 = "\xFF\xFE";
        for (char c : std::string("LWSC\n3\nAddNullObject B\n")) { u16.push_back(c); u16.push_back('\0'); }
        CHECK(ReadLwsBuffer(u16, "", m, &err) && m.nodes.size() == 2 && m.nodes[1].name == "B");
        CHECK(ReadLwsBuffer("LWSC 3\nAddNullObject A\n", "", m, &err) && m.nodes.size() == 2);   // the version on the first line
        CHECK(ReadLwsBuffer("LWSC\n1\nAddNullObject A\n", "", m, &err) && HasNote(m, "version 1 scene: read with the version 3 layout"));
    }
    // ---- truncated and damaged files ----
    {
        const std::string s = fx::BuildLws();
        const std::size_t inEnv = s.find("Behaviors");
        CHECK(inEnv != std::string::npos && Refused(s.substr(0, inEnv), "ends inside an envelope"));
        CHECK(Refused("LWSC\n3\nAddNullObject A\nChannel 0\n{ Envelope\n 1\n Key 1 0 3 0 0 0 0 0 0\n", "ends inside an envelope"));
        CHECK(Refused("LWSC\n3\nAddNullObject A\nChannel 0\n{ Envelope\n 99999999999\n Key 1 0 3\n}\n", "(budget)"));
        CHECK(Refused("LWSC\n3\nAddNullObject A\nChannel 0\n{ Envelope\n 4294967295\n Key 1 0 3\n}\n", "(budget)"));
        int accepted = 0, refusedN = 0;
        for (std::size_t len = 0; len < s.size(); len += 7) {   // cut anywhere: no crash, and what is read is sane
            ImportedModel m; std::string err;
            fx::FakeLoad fake; const IrrMeshLoader l = fake.Fn();
            if (ReadLwsBuffer(s.substr(0, len), "d/", m, &err, &l)) { ++accepted; CHECK(fx::Sane(m)); } else ++refusedN;
        }
        CHECK(accepted > 0 && refusedN > 0);
        for (int it = 0; it < 3000; ++it) {
            std::string t = s;
            const int flips = 1 + (int)(fx::Rnd() % 4);
            for (int f = 0; f < flips; ++f) t[(std::size_t)(fx::Rnd() % t.size())] ^= (char)(1u << (fx::Rnd() % 8));
            ImportedModel m; std::string err;
            fx::FakeLoad fake; const IrrMeshLoader l = fake.Fn();
            if (ReadLwsBuffer(t, "d/", m, &err, &l)) CHECK(fx::Sane(m));
        }
        // bad numbers in keys are skipped, not stored
        ImportedModel m; std::string err;
        const std::string bad = "LWSC\n3\nAddNullObject A\nChannel 0\n{ Envelope\n 5\n Key abc 0 3\n Key 1 nan 3\n Key 1 1e400 3\n Key 5 1e9 3\n Key 7 2 3\n}\n";
        CHECK(ReadLwsBuffer(bad, "", m, &err) && fx::Sane(m));
        CHECK(HasNote(m, "4 envelope key(s) that are not numbers or have a time out of range skipped"));
        const ImportedChannel* c = Chan(m, "A");
        CHECK(c && c->position.size() == 1 && Near(c->position[0].value.x, 7));
    }
    // ---- object files stay inside the scene's folder, and a scene cannot load a scene ----
    {
        const std::string s = "LWSC\n3\n"
            "LoadObject ../secret.lwo\nLoadObjectLayer 1 /etc/passwd\nLoadObject C:\\x.lwo\nLoadObject Objects/../../x.lwo\nLoadObject \\\\host\\s\\x.lwo\n"
            "LoadObject self.lws\nLoadObject sub/more.MOT\nLoadObject My Objects/a b.lwo\nLoadObjectLayer 2 10000009 good/ok.lwo\nLoadObject missing.lwo\nLoadObject ./up.lwo\n";
        fx::FakeLoad fake; const IrrMeshLoader l = fake.Fn();
        ImportedModel m; std::string err;
        CHECK(ReadLwsBuffer(s, "dir/", m, &err, &l));
        // only the three that stay inside, plus the one that is missing, reached the loader
        const std::vector<std::string> want = {"dir/My Objects/a b.lwo", "dir/good/ok.lwo", "dir/missing.lwo", "dir/up.lwo"};
        CHECK(fake.paths == want);
        for (const std::string& p : fake.paths) CHECK(p.find("..") == std::string::npos && p.find("secret") == std::string::npos && p.find("passwd") == std::string::npos && p.find("host") == std::string::npos && p.find("self.lws") == std::string::npos);
        CHECK(HasNote(m, "5 object file(s) refused") && HasNote(m, "2 object reference(s) to a scene file refused") && HasNote(m, "8 object(s) whose file could not be read"));
        CHECK(m.parts.size() == 3);
        // nothing readable: the scene is refused and says which file was first
        CHECK(Refused("LWSC\n3\nLoadObject ../x.lwo\n", "first that could not be read: ../x.lwo (refused", &l));
        CHECK(Refused("LWSC\n3\nLoadObject me.lws\n", "a scene may not load another scene", &l));
        CHECK(Refused("LWSC\n3\nLoadObject missing.lwo\n", "no such file", &l));
        // the items a refused path would have created are still nodes
        ImportedModel m2; std::string e2;
        CHECK(ReadLwsBuffer("LWSC\n3\nLoadObject ../x.lwo\nLoadObject Objects/ok.lwo\n", "dir/", m2, &e2, &l) && m2.nodes.size() == 4 && m2.parts.size() == 1);
    }
    TEST_MAIN_RESULT();
}
