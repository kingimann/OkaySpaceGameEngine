// Visual Script action nodes say once, by name, which component they are missing.
// Each node runs from On Update for 60 frames:
//   - on an object without the component: exactly one Console line, "'Host' has no X";
//   - with the component: no such line;
//   - with its Target wired to an object that lacks it: one line naming that object.
// Scene-wide nodes (Music, Stop Sequence) say "the scene has no X" instead.
// Loot Roll and Play Sequence need no component (Loot Roll drops what it rolls next to
// the object; Play Sequence adds its own Sequence Player), so they must stay quiet.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Components/AnimStateMachine.hpp>
#include <okay/Components/BehaviorTreeRunner.hpp>
#include <okay/Components/Character.hpp>
#include <okay/Components/Crafting.hpp>
#include <okay/Components/DialogueRunner.hpp>
#include <okay/Components/Equipment.hpp>
#include <okay/Components/FullBodyIK.hpp>
#include <okay/Components/GridPathAgent2D.hpp>
#include <okay/Components/Inventory.hpp>
#include <okay/Components/LineRenderer.hpp>
#include <okay/Components/MusicPlayer.hpp>
#include <okay/Components/NavMeshAgent.hpp>
#include <okay/Components/ParticleSystem.hpp>
#include <okay/Components/SequencePlayer.hpp>
#include <okay/Components/SurvivalComponents.hpp>
#include <okay/Components/TextRenderer.hpp>
#include <okay/Components/VisualScriptComponent.hpp>
#include <okay/Core/Log.hpp>
#include <okay/Physics/Rigidbody2D.hpp>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace okay;

namespace {

using AddFn = std::function<void(GameObject*)>;

struct Case {
    const char* type;
    const char* arg;          // the node's text argument ("" = none)
    const char* extra;        // more graph lines for node 2 (defaults), may be ""
    const char* component;    // what the warning names; nullptr = the node needs none
    bool sceneWide;           // "the scene has no X" rather than "'Host' has no X"
    int targetPin;            // its Target input, or -1
    AddFn add;                // gives an object the component
};

std::vector<std::string> g_lines;

std::string Graph(const Case& c, const std::string& targetName) {
    std::string t = "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 " + std::string(c.type);
    if (*c.arg) t += " " + std::string(c.arg);
    t += "\nexec 1 0 2\n";
    t += c.extra;
    if (!targetName.empty()) t += "default 2 " + std::to_string(c.targetPin) + " " + targetName + "\n";
    return t;
}

// Runs the case for 60 frames; returns the log lines that say something "has no".
std::vector<std::string> Run(const Case& c, bool hostHas, bool useTarget) {
    Scene s("MissingComponent");
    GameObject* host = s.CreateGameObject("Host");
    GameObject* other = s.CreateGameObject("Other");
    if (hostHas) c.add(host);
    auto* vc = host->AddComponent<VisualScriptComponent>();
    std::string err;
    if (!vc->LoadFromText(Graph(c, useTarget ? "Other" : ""), &err) || !err.empty()) {
        std::cout << "  graph for " << c.type << " did not load: " << err << "\n";
        CHECK(false);
        return {};
    }
    (void)other;
    g_lines.clear();
    s.Start();
    for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
    std::vector<std::string> out;
    for (const std::string& l : g_lines)
        if (l.find(" has no ") != std::string::npos) out.push_back(l);
    return out;
}

bool Has(const std::string& line, const std::string& what) { return line.find(what) != std::string::npos; }

void Report(const char* label, const Case& c, const std::vector<std::string>& lines) {
    std::cout << "  " << c.type << " " << label << ": " << lines.size() << " line(s)";
    if (!lines.empty()) std::cout << " -- " << lines.front();
    std::cout << "\n";
}

template <class T> AddFn Add() { return [](GameObject* g) { g->AddComponent<T>(); }; }

}  // namespace

int main() {
    RUN_SUITE("vs_missing_component");
    Log::sink = [](Log::Level, const std::string& m) { g_lines.push_back(m); };

    const AddFn bag = Add<Inventory>();
    const AddFn equip = [](GameObject* g) { g->AddComponent<Inventory>(); g->AddComponent<Equipment>(); };
    const std::vector<Case> cases = {
        {"AddItem", "wood", "", "Inventory", false, -1, bag},
        {"RemoveItem", "wood", "", "Inventory", false, -1, bag},
        {"LootRoll", "", "", nullptr, false, -1, bag},
        {"MusicPlay", "", "", "MusicPlayer", true, -1, Add<MusicPlayer>()},
        {"MusicStop", "", "", "MusicPlayer", true, -1, Add<MusicPlayer>()},
        {"MusicPause", "", "", "MusicPlayer", true, -1, Add<MusicPlayer>()},
        {"MusicResume", "", "", "MusicPlayer", true, -1, Add<MusicPlayer>()},
        {"MusicNext", "", "", "MusicPlayer", true, -1, Add<MusicPlayer>()},
        {"AnimTrigger", "jump", "", "AnimStateMachine", false, 0, Add<AnimStateMachine>()},
        {"AnimSetFloat", "speed", "default 2 0 1\n", "AnimStateMachine", false, 1, Add<AnimStateMachine>()},
        {"AnimSetBool", "run", "default 2 0 true\n", "AnimStateMachine", false, 1, Add<AnimStateMachine>()},
        {"NavMoveTo", "", "", "NavMeshAgent", false, 1, Add<NavMeshAgent>()},
        {"NavStop", "", "", "NavMeshAgent", false, 0, Add<NavMeshAgent>()},
        {"GridMoveTo", "", "default 2 0 1\ndefault 2 1 1\n", "GridPathAgent2D", false, 2, Add<GridPathAgent2D>()},
        {"GridStop", "", "", "GridPathAgent2D", false, 0, Add<GridPathAgent2D>()},
        {"BtSet", "x", "default 2 0 1\n", "BehaviorTreeRunner", false, 1, Add<BehaviorTreeRunner>()},
        {"ParticlePlay", "", "", "ParticleSystem", false, 0, Add<ParticleSystem>()},
        {"ParticleStop", "", "", "ParticleSystem", false, 0, Add<ParticleSystem>()},
        {"ParticleBurst", "", "default 2 0 3\n", "ParticleSystem", false, 1, Add<ParticleSystem>()},
        {"IkLook", "", "", "FullBodyIK", false, 2, Add<FullBodyIK>()},
        {"IkHand", "right", "", "FullBodyIK", false, 2, Add<FullBodyIK>()},
        {"EquipItem", "helmet", "default 2 0 Head\n", "Equipment", false, -1, equip},
        {"CraftItem", "plank", "", "Crafting", false, -1, Add<Crafting>()},
        {"PlaySequence", "", "", nullptr, false, -1, Add<SequencePlayer>()},
        {"StopSequence", "", "", "SequencePlayer", true, -1, Add<SequencePlayer>()},
        {"SetText", "self", "default 2 0 hello\n", "TextRenderer", false, 1, Add<TextRenderer>()},
        {"SetLinePoints", "", "", "LineRenderer", false, 1, Add<LineRenderer>()},
        {"ClearTrail", "", "", "TrailRenderer", false, 0, Add<TrailRenderer>()},
        {"PlayClip", "Wave", "", "Character or ModelAnimator", false, 0, Add<Character>()},
        {"Revive", "", "", "HealthStat or AnimStateMachine", false, -1, Add<HealthStat>()},
        {"StartDialogue", "", "", "DialogueRunner", false, -1, Add<DialogueRunner>()},
        {"AddExplosionForce", "", "", "Rigidbody2D", false, -1, Add<Rigidbody2D>()},
    };
    CHECK(cases.size() == 32);

    for (const Case& c : cases) {
        // Without the component.
        const std::vector<std::string> without = Run(c, false, false);
        Report("without", c, without);
        if (!c.component) {
            CHECK(without.empty());
        } else {
            CHECK(without.size() == 1);
            if (without.size() == 1) {
                const std::string who = c.sceneWide ? "the scene" : "'Host'";
                CHECK(Has(without[0], who + " has no " + c.component));
            }
        }
        // With the component.
        const std::vector<std::string> with = Run(c, true, false);
        Report("with", c, with);
        CHECK(with.empty());
        // With a Target that lacks it (the graph's own object has it).
        if (c.targetPin >= 0) {
            const std::vector<std::string> target = Run(c, true, true);
            Report("target", c, target);
            CHECK(target.size() == 1);
            if (target.size() == 1) CHECK(Has(target[0], std::string("'Other' has no ") + c.component));
        }
    }

    // Start Dialogue by name: no runner by that name anywhere in the scene.
    {
        const Case named{"StartDialogue", "Shopkeeper", "", "DialogueRunner named 'Shopkeeper'", true, -1, Add<DialogueRunner>()};
        const std::vector<std::string> lines = Run(named, false, false);
        Report("named", named, lines);
        CHECK(lines.size() == 1);
        if (lines.size() == 1) CHECK(Has(lines[0], "the scene has no DialogueRunner named 'Shopkeeper'"));
    }

    // An engine-API node shares the helper: one line, not one per frame.
    {
        const Case api{"NavMeshAgent.Stop/0", "", "", "NavMeshAgent", false, -1, Add<NavMeshAgent>()};
        const std::vector<std::string> lines = Run(api, false, false);
        Report("without", api, lines);
        CHECK(lines.size() == 1);
        if (lines.size() == 1) CHECK(Has(lines[0], "'Host' has no NavMeshAgent"));
    }

    Log::sink = nullptr;
    TEST_MAIN_RESULT();
}
