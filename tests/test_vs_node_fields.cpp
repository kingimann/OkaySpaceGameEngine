// Visual Script node fields (issue #888, finding 34 part A). Nodes that take a
// prefab, a clip, a tag, a layer, a graph file, an object or a variable name get
// the same slot or dropdown as the node that already did it right (Spawn,
// Play Clip, Find With Tag, Raycast 3D, Run Tree, Set Var). A node that can't run
// with its text field empty says so twice: the editor's red "Missing" badge and
// a Check Graph warning, both read from one list. And none of this changes the
// saved text: what a slot or dropdown writes is the string the old text field
// held, so an old graph loads, upgrades and saves byte for byte the same.
//
// The checks read the editor's own functions (VsArgKind, VsArgChoice,
// VsPinKind, VsPinChoice, VsRequiredWhat, VsNodeReadiness) and the engine's
// ValidateGraphText, so they test what the editor draws and what Check Graph says.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include <Okay.hpp>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor::vsed;
using okay::vs::VsRefKind;

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// The input pin of `type` whose label is `label` (any case), or -1.
int PinNamed(const VsDef& d, const char* label) {
    for (int i = 0; i < (int)d.dataIn.size(); ++i)
        if (d.dataIn[(std::size_t)i] && Lower(d.dataIn[(std::size_t)i]) == Lower(label)) return i;
    return -1;
}

bool HasRequired(const std::vector<okay::vs::VsIssue>& v, int node, const std::string& what) {
    for (const auto& is : v)
        if (is.node == node && is.message.find("Required: " + what) != std::string::npos) return true;
    return false;
}
bool HasAnyRequired(const std::vector<okay::vs::VsIssue>& v, int node) {
    for (const auto& is : v)
        if (is.node == node && is.message.find("Required: ") != std::string::npos) return true;
    return false;
}

const char* Str(const char* s) { return s ? s : "(none)"; }

} // namespace

int main() {
    RUN_SUITE("vs_node_fields");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- 1) Each node's text field: slot kind, plain storage, dropdown, required ----
    struct ArgRow {
        const char* type;
        VsRefKind ref;          // the slot the text field becomes (None = no slot)
        bool plain;             // the slot writes the plain name / path the field held
        const char* choice;     // VsChoiceKindName of its dropdown ("value" = none)
        const char* required;   // VsRequiredWhat(type, -1), nullptr = optional
    };
    const ArgRow args[] = {
        // Prefab slot, like Spawn. The runtime reads a plain path, and Pool Get keys
        // its pool by that text, so the slot writes the path itself.
        {"SpawnProjectile", VsRefKind::Prefab, true, "value", "prefab"},
        {"SpawnEvery",      VsRefKind::Prefab, true, "value", "prefab"},
        {"PoolGet",         VsRefKind::Prefab, true, "value", "prefab"},
        // Clip list, like Play Clip.
        {"CrossfadeClip",   VsRefKind::None, false, "clip", "clip name"},
        // Graph slot, like Run Tree. "file.okayvs|Event" still types in.
        {"RunGraph",        VsRefKind::Graph, true, "value", "graph file"},
        // Object slots. The runtime finds the object by name.
        {"SwitchCamera",    VsRefKind::Object, true, "value", "camera"},
        {"StartDialogue",   VsRefKind::Object, true, "value", nullptr},   // empty = this object's Dialogue Runner
        // Can't run empty; stays a typed name (a sequence file name, not a slot).
        {"PlaySequence",    VsRefKind::None, false, "value", "sequence name"},
        // Object tags, like Find With Tag.
        {"OnTagEnter",      VsRefKind::None, false, "tag", nullptr},
        {"OnTagStay",       VsRefKind::None, false, "tag", nullptr},
        {"OnTagExit",       VsRefKind::None, false, "tag", nullptr},
        {"OnCollisionTag",  VsRefKind::None, false, "tag", nullptr},
        {"FindByTag",       VsRefKind::None, false, "tag", nullptr},
        // A tween's own label (Tweener::SetTag), not an object tag: the list is the
        // tween tags this graph already uses.
        {"TweenMove",       VsRefKind::None, false, "tween tag", nullptr},
        {"TweenMoveSpeed",  VsRefKind::None, false, "tween tag", nullptr},
        {"TweenKillTag",    VsRefKind::None, false, "tween tag", nullptr},
        {"TweenPauseTag",   VsRefKind::None, false, "tween tag", nullptr},
        {"TweenPlayTag",    VsRefKind::None, false, "tween tag", nullptr},
        // Layers, like Raycast 3D.
        {"SphereCast3D",    VsRefKind::None, false, "layer", nullptr},
        {"RaycastAll3D",    VsRefKind::None, false, "layer", nullptr},
        // Variable names, like Set Var.
        {"SetObjectVar",    VsRefKind::None, false, "variable", nullptr},
        {"GetObjectVar",    VsRefKind::None, false, "variable", nullptr},
        {"SetStoryVar",     VsRefKind::None, false, "story variable", nullptr},
        {"GetStoryVar",     VsRefKind::None, false, "story variable", nullptr},
        {"SetLocal",        VsRefKind::None, false, "local variable", nullptr},
        {"GetLocal",        VsRefKind::None, false, "local variable", nullptr},
        // The nodes that already did it right keep doing it.
        {"Spawn",           VsRefKind::Prefab, false, "value", "prefab"},
        {"PlayClip",        VsRefKind::None, false, "clip", "clip name"},
        {"FindWithTag",     VsRefKind::None, false, "tag", nullptr},
        {"Raycast3D",       VsRefKind::None, false, "layer", nullptr},
        {"BtRun",           VsRefKind::Graph, false, "value", nullptr},
        {"SetVar",          VsRefKind::None, false, "variable", "variable name"},
        {"FindObject",      VsRefKind::Object, false, "value", nullptr},
    };
    std::printf("  text fields:\n");
    for (const ArgRow& r : args) {
        const VsDef* d = FindDef(r.type);
        CHECK(d != nullptr);
        if (!d) { std::printf("    %s: not in the registry\n", r.type); continue; }
        CHECK(d->param == 1);
        const VsRefKind ref = VsArgKind(r.type);
        const bool plain = VsArgPlainRef(r.type);
        const std::string choice = VsChoiceKindName(VsArgChoice(r.type));
        const char* req = VsRequiredWhat(r.type, -1);
        const bool ok = ref == r.ref && plain == r.plain && choice == r.choice &&
                        std::string(Str(req)) == Str(r.required);
        std::printf("    %-16s slot %-28s plain %d  list %-15s required %-14s %s\n", r.type,
                    okay::vs::VsRefKindName(ref), (int)plain, choice.c_str(), Str(req), ok ? "ok" : "WRONG");
        CHECK(ref == r.ref);
        CHECK(plain == r.plain);
        CHECK(choice == r.choice);
        CHECK(std::string(Str(req)) == Str(r.required));
        // A text field is a slot or a dropdown, never both.
        CHECK(!(ref != VsRefKind::None && VsArgChoice(r.type) != VsChoiceKind::None));
    }

    // ---- 2) Text pins: "Tag" gets the tag list, "Object name" an object slot ----
    struct PinRow { const char* type; const char* label; const char* choice; VsRefKind ref; bool plain; };
    const PinRow pins[] = {
        {"CountNearby", "Tag", "tag", VsRefKind::None, false},
        {"NearestObject", "Tag", "tag", VsRefKind::None, false},
        {"FarthestObject", "Tag", "tag", VsRefKind::None, false},
        {"GlCountObjectsWithTag", "Tag", "tag", VsRefKind::None, false},
        {"GlIsAnyTaggedInRange", "Tag", "tag", VsRefKind::None, false},
        {"GlPickRandomTagged", "Tag", "tag", VsRefKind::None, false},
        {"GlDestroyAllWithTag", "Tag", "tag", VsRefKind::None, false},
        {"GlHideAllWithTag", "Tag", "tag", VsRefKind::None, false},
        {"GlShowAllWithTag", "Tag", "tag", VsRefKind::None, false},
        {"GlSetTagOnObject", "Tag", "tag", VsRefKind::None, false},
        {"GlCountTaggedInRange", "Tag", "tag", VsRefKind::None, false},
        {"FindByTag", "Tag", "tag", VsRefKind::None, false},
        {"CameraFollow", "Object name", "value", VsRefKind::Object, true},
        {"UITheme.ApplyTo/1", "Object Name", "value", VsRefKind::Object, true},
        {"IK.SetLimbTarget/2", "Object Name", "value", VsRefKind::Object, true},
    };
    std::printf("  text pins:\n");
    for (const PinRow& r : pins) {
        const VsDef* d = FindDef(r.type);
        CHECK(d != nullptr);
        if (!d) { std::printf("    %s: not in the registry\n", r.type); continue; }
        const int i = PinNamed(*d, r.label);
        CHECK(i >= 0);
        if (i < 0) { std::printf("    %s: no pin named %s\n", r.type, r.label); continue; }
        const okay::vs::PinType pt = okay::vs::InputType(r.type, i);
        const char* label = d->dataIn[(std::size_t)i];
        const std::string choice = VsChoiceKindName(VsPinChoice(r.type, i, (char)pt, label));
        const VsRefKind ref = VsPinKind(r.type, i, pt, label);
        const bool plain = VsPinPlainRef(r.type, i, pt, label);
        const bool ok = choice == r.choice && ref == r.ref && plain == r.plain;
        std::printf("    %-22s pin %d %-12s (%s) list %-6s slot %-12s plain %d %s\n", r.type, i, label,
                    okay::vs::PinTypeName(pt), choice.c_str(), okay::vs::VsRefKindName(ref), (int)plain, ok ? "ok" : "WRONG");
        CHECK(pt == okay::vs::PinType::Text);
        CHECK(choice == r.choice);
        CHECK(ref == r.ref);
        CHECK(plain == r.plain);
    }
    // Every other registry pin the same rules catch, so the reach is visible.
    {
        int extra = 0;
        for (const VsDef& d : Registry()) {
            for (int i = 0; i < (int)d.dataIn.size(); ++i) {
                const okay::vs::PinType pt = okay::vs::InputType(d.type, i);
                const char* label = d.dataIn[(std::size_t)i];
                const bool tag = VsPinChoice(d.type, i, (char)pt, label) != VsChoiceKind::None;
                const bool plainObj = VsPinPlainRef(d.type, i, pt, label);
                if (!tag && !plainObj) continue;
                bool listed = false;
                for (const PinRow& r : pins) if (std::string(r.type) == d.type && Lower(r.label) == Lower(label)) listed = true;
                if (listed) continue;
                ++extra;
                std::printf("    also: %s pin %d \"%s\" -> %s\n", d.type, i, label, tag ? "tag list" : "object slot (name)");
                // Only text pins that hold a tag or an object's name.
                CHECK(pt == okay::vs::PinType::Text);
                CHECK(Lower(label) == "tag" || Lower(label) == "object name");
            }
        }
        std::printf("    %d more pin(s) follow the same rule\n", extra);
    }

    // ---- 3) Required: the editor badge and Check Graph say the same thing ----
    {
        auto none = [](int) { return false; };
        const char* required[] = {"SpawnProjectile", "SpawnEvery", "PoolGet", "CrossfadeClip", "RunGraph",
                                  "SwitchCamera", "PlaySequence", "Spawn", "PlayClip"};
        std::printf("  required, empty:\n");
        for (const char* t : required) {
            const char* what = VsRequiredWhat(t, -1);
            CHECK(what != nullptr);
            if (!what) { std::printf("    %-16s editor: optional (no badge)\n", t); continue; }
            std::string miss;
            const VsReady ready = VsNodeReadiness(t, false, none, false, &miss);
            CHECK(ready == VsReady::Missing);
            CHECK(miss == std::string("Required: ") + what);
            const auto empty = okay::vs::ValidateGraphText("node 1 OnStart\nentry OnStart 1\nnode 2 " + std::string(t) + "\nexec 1 0 2\n");
            const bool warned = HasRequired(empty, 2, what);
            std::printf("    %-16s editor: %-26s Check Graph: %s\n", t, miss.c_str(), warned ? "warns" : "SILENT");
            CHECK(warned);
            for (const auto& is : empty) if (is.node == 2 && is.message.find("Required: ") != std::string::npos) CHECK(is.severity == 1);
            // Filled in: ready, and no warning.
            CHECK(VsNodeReadiness(t, true, none, false, &miss) == VsReady::Ready);
            const auto filled = okay::vs::ValidateGraphText("node 1 OnStart\nentry OnStart 1\nnode 2 " + std::string(t) + " x\nexec 1 0 2\n");
            CHECK(!HasAnyRequired(filled, 2));
        }
        // Start Dialogue empty is this object's Dialogue Runner: fine as it is.
        std::string miss;
        CHECK(VsNodeReadiness("StartDialogue", false, none, false, &miss) == VsReady::Ready);
        CHECK(!HasAnyRequired(okay::vs::ValidateGraphText("node 1 OnStart\nentry OnStart 1\nnode 2 StartDialogue\nexec 1 0 2\n"), 2));
        CHECK(VsArgDefaultsSelf("StartDialogue"));

        // One list: every text field the editor marks required, Check Graph warns about.
        int both = 0;
        for (const VsDef& d : Registry()) {
            const char* what = VsRequiredWhat(d.type, -1);
            if (!what) continue;
            const auto v = okay::vs::ValidateGraphText("node 2 " + std::string(d.type) + "\n");
            const bool warned = HasRequired(v, 2, what);
            if (!warned) std::printf("    %s: the editor says \"Required: %s\", Check Graph is silent\n", d.type, what);
            CHECK(warned);
            ++both;
        }
        std::printf("    %d text fields are required in the editor and in Check Graph\n", both);
    }

    // ---- 4) The saved text does not change ----
    {
        // What a slot writes for a picked object or a dropped file.
        Scene scene("fields");
        GameObject* cam = scene.CreateGameObject("CamB");
        cam->AddComponent<Camera>();
        GameObject* player = scene.CreateGameObject("Player");
        CHECK(!cam->Guid().empty() && !player->Guid().empty());
        CHECK(VsSlotObjectValue(*cam, true) == "CamB");                          // Switch Camera: its name
        CHECK(VsSlotObjectValue(*cam, false) == "@guid:" + cam->Guid());         // Find Object: still a guid
        CHECK(VsSlotFileValue(VsRefKind::Prefab, "Assets/bullet.okayprefab", "", true) == "Assets/bullet.okayprefab");
        CHECK(VsSlotFileValue(VsRefKind::Prefab, "Assets/bullet.okayprefab", "", false) == "@prefab:Assets/bullet.okayprefab");
        // Run Graph: a new file keeps the event after the bar.
        CHECK(VsSlotFileValue(VsRefKind::Graph, "Assets/boss.okayvs", "old.okayvs|Phase2", true) == "Assets/boss.okayvs|Phase2");
        CHECK(VsSlotFileValue(VsRefKind::Graph, "Assets/boss.okayvs", "old.okayvs", true) == "Assets/boss.okayvs");

        // An old graph with every changed node, as people typed them.
        const std::string old =
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 SpawnProjectile Assets/bullet.okayprefab\n"
            "node 3 SpawnEvery cube.okayprefab\n"
            "node 4 PoolGet cube.okayprefab\n"
            "node 5 CrossfadeClip Wave\n"
            "node 6 RunGraph \"sub.okayvs|Phase 2\"\n"
            "node 7 SwitchCamera CamB\n"
            "node 8 StartDialogue Player\n"
            "node 9 PlaySequence intro\n"
            "node 10 OnTagEnter Player\nentry OnTagEnter:Player 10\n"
            "node 11 OnTagStay Player\nentry OnTagStay:Player 11\n"
            "node 12 OnTagExit Player\nentry OnTagExit:Player 12\n"
            "node 13 OnCollisionTag Enemy\nentry OnCollisionTag:Enemy 13\n"
            "node 14 FindByTag Player\ndefault 14 0 Enemy\n"
            "node 15 TweenMove door\nnode 16 TweenMoveSpeed door\nnode 17 TweenKillTag door\n"
            "node 18 TweenPauseTag door\nnode 19 TweenPlayTag door\n"
            "node 20 SphereCast3D \"Default, Enemy\"\nnode 21 RaycastAll3D Enemy\n"
            "node 22 SetObjectVar hp\nnode 23 GetObjectVar hp\nnode 24 SetStoryVar met_king\n"
            "node 25 GetStoryVar met_king\nnode 26 SetLocal i\nnode 27 GetLocal i\n"
            "node 28 CountNearby\ndefault 28 1 Enemy\nnode 29 NearestObject\ndefault 29 1 Enemy\n"
            "node 30 FarthestObject\ndefault 30 1 Enemy\nnode 31 GlCountObjectsWithTag\ndefault 31 0 Enemy\n"
            "node 32 GlSetTagOnObject\ndefault 32 0 Boss\n"
            "node 33 CameraFollow\ndefault 33 0 Player\n"
            "node 34 UITheme.ApplyTo/1\ndefault 34 0 Player\n"
            "node 35 IK.SetLimbTarget/2\ndefault 35 0 hand_l\ndefault 35 1 Player\n";
        VsEditGraph g;
        g.fromText(old);
        const std::string saved = g.toText();
        VsEditGraph again;
        again.fromText(saved);
        CHECK(again.toText() == saved);   // toText -> fromText -> toText: byte for byte
        // Each value comes back exactly as typed.
        auto argOf = [&](int id) { const VsEditNode* n = again.find(id); return n ? n->arg : std::string("?"); };
        auto pinOf = [&](int id, int pin) {
            const VsEditNode* n = again.find(id);
            if (!n) return std::string("?");
            auto it = n->pinDefault.find(pin);
            return it == n->pinDefault.end() ? std::string() : it->second;
        };
        CHECK(argOf(2) == "Assets/bullet.okayprefab");
        CHECK(argOf(6) == "sub.okayvs|Phase 2");
        CHECK(argOf(7) == "CamB");
        CHECK(argOf(20) == "Default, Enemy");
        CHECK(pinOf(14, 0) == "Enemy");
        CHECK(pinOf(33, 0) == "Player");
        CHECK(pinOf(35, 1) == "Player");

        // Opening it in the editor (VsUpgradeGraph runs on open) leaves every plain
        // name and path alone, though the scene has a CamB and a Player with guids.
        VsEditGraph opened;
        opened.fromText(old);
        const bool upgraded = VsUpgradeGraph(opened, scene);
        if (upgraded || opened.toText() != saved) {
            std::printf("  opening the old graph rewrote it:\n");
            std::istringstream a(saved), b(opened.toText());
            std::string la, lb;
            while (std::getline(a, la) && std::getline(b, lb)) if (la != lb) std::printf("    was: %s\n    now: %s\n", la.c_str(), lb.c_str());
        }
        CHECK(!upgraded);
        CHECK(opened.toText() == saved);

        // The runtime loads it.
        std::string err;
        CHECK(okay::vs::NodeGraph::Parse(saved, &err) != nullptr);
        if (!err.empty()) std::printf("  runtime: %s\n", err.c_str());

        // Nodes that already wrote references keep doing so (Spawn's slot, Find Object).
        VsEditGraph legacy;
        legacy.fromText("node 1 Spawn Assets/b.okayprefab\nnode 2 FindObject Player\n");
        CHECK(VsUpgradeGraph(legacy, scene));
        CHECK(legacy.find(1)->arg == "@prefab:Assets/b.okayprefab");
        CHECK(legacy.find(2)->arg == "@guid:" + player->Guid());
    }

    // ---- 5) Spawn's prefab check reads a dropped prefab (@prefab:) as its file ----
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "okay_vs_node_fields";
        fs::create_directories(dir / "Assets", ec);
        { std::ofstream f(dir / "Assets" / "b.okayprefab"); f << "okayscene 1\n"; }
        const auto v = okay::vs::ValidateGraphText("node 1 OnStart\nentry OnStart 1\nnode 2 Spawn @prefab:Assets/b.okayprefab\nexec 1 0 2\n",
                                                   dir.string());
        bool missing = false;
        for (const auto& is : v) if (is.node == 2 && is.message.find("doesn't exist") != std::string::npos) missing = true;
        CHECK(!missing);
        const auto gone = okay::vs::ValidateGraphText("node 1 OnStart\nentry OnStart 1\nnode 2 Spawn @prefab:Assets/nope.okayprefab\nexec 1 0 2\n",
                                                      dir.string());
        bool told = false;
        for (const auto& is : gone) if (is.node == 2 && is.message.find("doesn't exist") != std::string::npos) told = true;
        CHECK(told);
        // Empty: one "Required: prefab" warning, not that and an error too.
        const auto empty = okay::vs::ValidateGraphText("node 1 OnStart\nentry OnStart 1\nnode 2 Spawn\nexec 1 0 2\n", dir.string());
        int forNode = 0;
        for (const auto& is : empty) if (is.node == 2 && (is.message.find("prefab") != std::string::npos)) ++forNode;
        CHECK(forNode == 1);
        fs::remove_all(dir, ec);
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
