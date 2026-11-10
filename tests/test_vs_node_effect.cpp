// A node has to do something. Every value node of the gameplay library, the logic library, the
// smart-logic and logic-round sets gives different answers for different inputs (a node that
// returns one constant fails). Nodes that read the scene (their only inputs are objects) are
// proven by test_vs_gameplay_library, which compares a scene with the node against an empty one.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include <cctype>
#include <cstring>
#include <set>
#include <string>

using namespace okay;
using namespace okay::editor::vsed;

static std::string Str(const vs::VsValue& v) {
    char b[96];
    std::snprintf(b, sizeof b, "%.4f|%d|", (double)v.AsFloat(), (int)v.AsBool());
    return std::string(b) + v.AsString();
}

// Gameplay nodes whose answer comes from the scene: proven in vs_gameplay_library (scene with the
// node against an empty scene).
static bool SceneReader(const char* t) {
    static const char* k[] = {
#include "generated/gplib_scene_readers.inc"
        // Smart-logic nodes that look at other objects or the saved game; their own suites cover them.
        "HappenedNear", "LbSpike", "InCone", "CountNearby", "NearestObject", "FarthestObject", "RecallValue", "LineOfSight", "SortByDistance",
    };
    for (const char* x : k) if (!std::strcmp(x, t)) return true;
    return false;
}

static bool InFamily(const char* t) {
    if (std::strncmp(t, "Lb", 2) == 0) return true;
    if (std::strncmp(t, "Gl", 2) == 0 && t[2] >= 'A' && t[2] <= 'Z') return true;
    const char* demo = VsPlaygroundForNode(t);
    return demo && (!std::strcmp(demo, "smart") || !std::strcmp(demo, "logic"));
}

int main() {
    RUN_SUITE("vs_node_effect");
    static const char* nums[] = {"-3", "0", "0.5", "1", "2", "7", "100", "-0.25", "0.9", "45", "0.3"};
    static const char* texts[] = {"\"\"", "\"a\"", "\"abc\"", "\"Hello World\"", "\"7\"", "\"x,y\"", "\"red-blue\"", "\"-\""};
    int swept = 0, sceneReaders = 0, constant = 0;
    std::vector<std::string> readers, listNodes;
    for (const VsDef& d : Registry()) {
        if (!InFamily(d.type)) continue;
        const vs::NodeSignature* sig = vs::SignatureFor(d.type);
        if (!sig || sig->execIn || sig->outs.empty()) continue;
        bool sweepable = false, needsList = false;
        for (char c : sig->ins) {
            if (c == 'N' || c == 'B' || c == 'T' || c == 'A') sweepable = true;
            if (c == 'L' || c == 'M' || c == 'V' || c == 'O') needsList = true;
        }
        if (SceneReader(d.type)) { ++sceneReaders; continue; }
        if (needsList) { listNodes.push_back(d.type); continue; }
        if (!sweepable) { ++sceneReaders; readers.push_back(d.type); continue; }
        std::set<std::string> seen;
        for (int k = 0; k < 40; ++k) {
            std::string g = "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 " + std::string(d.type) + "\n";
            for (int i = 0; i < (int)sig->ins.size(); ++i) {
                const char c = sig->ins[(std::size_t)i];
                const int pick = (k % 4 == 0) ? (k / 4) % 11 : (k * (i + 2) + i * 3 + k / 3) % 11;
                if (c == 'N' || c == 'A') g += "default 2 " + std::to_string(i) + " " + nums[pick] + "\n";
                else if (c == 'B') g += "default 2 " + std::to_string(i) + " " + (((k >> (i % 4)) & 1) ? "true" : "false") + "\n";
                else if (c == 'T') g += "default 2 " + std::to_string(i) + " " + texts[(k % 4 == 0 ? k / 4 : k + i) % 8] + "\n";
            }
            g += "node 3 SetVar out\nexec 1 0 3\ndata 3 0 2 0\n";
            Scene s; s.physicsEnabled = false;
            GameObject* me = s.CreateGameObject("Me");
            auto* vc = me->AddComponent<VisualScriptComponent>();
            std::string err;
            if (!vc->LoadFromText(g, &err)) { std::printf("  %s: %s\n", d.type, err.c_str()); CHECK(false); break; }
            s.Start();
            for (int f = 0; f < 6; ++f) s.Update(0.5f);
            seen.insert(Str(vc->GetVariable("out")));
        }
        ++swept;
        if (seen.size() < 2) { ++constant; std::printf("  %s gave one answer for every input: %s\n", d.type, seen.empty() ? "(none)" : seen.begin()->c_str()); }
        CHECK(seen.size() >= 2);
    }
    std::printf("  swept %d value nodes; %d read the scene (own suites); %d take lists or objects (own suites); constant: %d\n", swept, sceneReaders, (int)listNodes.size(), constant);
    CHECK(swept > 200);
    TEST_MAIN_RESULT();
}
