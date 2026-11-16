// Asset files named next to a scene (#888 finding 28). The shipped examples
// name their cutscene, behaviour trees and weapons by bare file name
// ("intro.okayseq", "GuardBrain.okaybt", the "sword" in weapons.okayweapon).
// Copied into a project's Assets/<name>/ folder, the scene must still find
// them: after the project folder (today's lookup, which still wins), the
// folder the scene was loaded from is searched too.
//
// Also checks that the same scenes still work opened as their own project
// (the project folder is the example folder), the way they always did.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <filesystem>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

const fs::path kExamples = fs::path(OKAY_SOURCE_DIR) / "examples";

// The base folders the editor sets on Play (EditorState::Play and the
// project open), and the project's weapon list (<project>/weapons.okayweapon).
void UseProject(const fs::path& project) {
    const std::string dir = project.string();
    Character::SetAssetBaseDir(dir);
    BehaviorTreeRunner::SetAssetBaseDir(dir);
    BehaviorTree::Forget(std::string());   // a tree cached from another folder must not answer
    WeaponBook::Get().Clear();
    WeaponBook::Get().Load((project / "weapons.okayweapon").string());   // missing in the temp project: an empty list
}

// Load `scene` and check the cutscene file is found.
void CheckCutscene(const fs::path& scene, const char* label) {
    Scene s("cut");
    std::string err;
    const bool ok = SceneSerializer::LoadFromFile(s, scene.string(), &err);
    if (!ok) std::cerr << "  (" << label << ": " << err << ")\n";
    CHECK(ok);
    int players = 0;
    for (const auto& go : s.Objects()) {
        for (auto* sp : go->GetComponents<SequencePlayer>()) {
            ++players;
            std::string e;
            const bool loaded = sp->Load(&e);
            if (!loaded) std::cerr << "  (" << label << ": sequence '" << sp->sequencePath << "': " << e << ")\n";
            CHECK(loaded);
            CHECK(sp->HasSequence());
            CHECK(sp->Length() > 0.0f);
            // The nested sequence the cutscene plays sits next to it too.
            CHECK(sp->library().Find("glance.okayseq") != nullptr);
        }
    }
    CHECK(players == 1);
}

// Load `scene`, run it a few frames, and check every runner's tree (and every
// sub-tree those run) was read.
void CheckTrees(const fs::path& scene, int wantRunners, const char* label) {
    Scene s("bt");
    std::string err;
    const bool ok = SceneSerializer::LoadFromFile(s, scene.string(), &err);
    if (!ok) std::cerr << "  (" << label << ": " << err << ")\n";
    CHECK(ok);
    std::vector<BehaviorTreeRunner*> runners;
    for (const auto& go : s.Objects())
        for (auto* r : go->GetComponents<BehaviorTreeRunner>()) runners.push_back(r);
    CHECK((int)runners.size() == wantRunners);
    for (auto* r : runners) {
        std::string e;
        const bool loaded = r->LoadTree(std::string(), &e);
        if (!loaded) std::cerr << "  (" << label << ": tree '" << r->treePath << "': " << e << ")\n";
        CHECK(loaded);
        CHECK(r->Tree() != nullptr);
    }
    s.Start();
    for (int i = 0; i < 6; ++i) s.Update(0.1f);
    for (auto* r : runners) {
        // loop.okaybt runs itself on purpose: that is refused, not unreadable.
        const std::string& e = r->LastError();
        const bool unreadable = e.find("cannot") != std::string::npos || e.find("can't") != std::string::npos ||
                                e.find("open") != std::string::npos;
        if (unreadable) std::cerr << "  (" << label << ": runner '" << r->treePath << "': " << e << ")\n";
        CHECK(!unreadable);
    }
    Game::ClearPause(BehaviorTreeRunner::kBreakPause);   // the breakpoint demo pauses on purpose
}

// Load the weapons arena and check the fighter's sword and the gun's pistol
// come from weapons.okayweapon (12 damage and 24 spare rounds, not the scene's own 15 and -1).
void CheckWeapons(const fs::path& scene, const char* label) {
    Scene s("arena");
    std::string err;
    const bool ok = SceneSerializer::LoadFromFile(s, scene.string(), &err);
    if (!ok) std::cerr << "  (" << label << ": " << err << ")\n";
    CHECK(ok);
    GameObject* fighter = s.Find("Fighter");
    GameObject* gun = s.Find("Gun");
    CHECK(fighter && gun);
    if (!fighter || !gun) return;
    auto* melee = fighter->GetComponent<MeleeAttacker>();
    auto* ranged = gun->GetComponent<RangedWeapon>();
    CHECK(melee && ranged);
    if (!melee || !ranged) return;
    const WeaponDefinition* sword = melee->ChosenWeapon();
    if (!sword) std::cerr << "  (" << label << ": the fighter's \"" << melee->weapon << "\" was not found)\n";
    CHECK(sword != nullptr);
    if (sword) { CHECK(sword->id == "sword"); CHECK_NEAR(sword->damage, 12.0f, 1e-4); }
    // The pistol has 24 spare rounds; the scene's own numbers say -1 (never run out).
    const int spare = ranged->Spare();
    if (spare != 24) std::cerr << "  (" << label << ": the gun's \"" << ranged->weapon << "\" was not found)\n";
    CHECK(spare == 24);
    CHECK(ranged->Rounds() == 8);
}

} // namespace

int main() {
    RUN_SUITE("scene-relative asset paths");
    const fs::path cwd0 = fs::current_path();
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / ("okay_scene_rel_" + std::to_string((unsigned long long)std::hash<std::string>{}(cwd0.string()) % 1000000ULL));
    fs::remove_all(root, ec);
    const fs::path project = root / "Proj";
    const fs::path assets = project / "Assets";
    fs::create_directories(assets, ec);
    for (const char* name : {"cutscene", "behavior_trees", "weapons"})
        fs::copy(kExamples / name, assets / name, fs::copy_options::recursive, ec);
    CHECK(fs::exists(assets / "cutscene" / "intro.okayscene"));
    // Neither the working folder nor the project folder holds the files.
    fs::current_path(root, ec);

    {
        RUN_SUITE("copied into a project's Assets folder");
        UseProject(project);
        CheckCutscene(assets / "cutscene" / "intro.okayscene", "Assets/cutscene");
        CheckTrees(assets / "behavior_trees" / "subtree_demo.okayscene", 5, "Assets/subtree_demo");
        CheckTrees(assets / "behavior_trees" / "breakpoint_demo.okayscene", 1, "Assets/breakpoint_demo");
        CheckWeapons(assets / "weapons" / "arena.okayscene", "Assets/weapons");
    }
    {
        RUN_SUITE("each example opened as its own project");
        UseProject(kExamples / "cutscene");
        CheckCutscene(kExamples / "cutscene" / "intro.okayscene", "examples/cutscene");
        UseProject(kExamples / "behavior_trees");
        CheckTrees(kExamples / "behavior_trees" / "subtree_demo.okayscene", 5, "examples/subtree_demo");
        CheckTrees(kExamples / "behavior_trees" / "breakpoint_demo.okayscene", 1, "examples/breakpoint_demo");
        UseProject(kExamples / "weapons");
        CheckWeapons(kExamples / "weapons" / "arena.okayscene", "examples/weapons");
    }
    {
        RUN_SUITE("the project folder still wins over the scene folder");
        // A project-level weapon list with its own "sword": that one is used.
        UseProject(project);
        WeaponBook::Get().LoadText("okayweapon 1\nweapon \"sword\"\n  kind melee\n  damage 33\nend\n");
        Scene s("arena");
        CHECK(SceneSerializer::LoadFromFile(s, (assets / "weapons" / "arena.okayscene").string()));
        GameObject* fighter = s.Find("Fighter");
        auto* melee = fighter ? fighter->GetComponent<MeleeAttacker>() : nullptr;
        const WeaponDefinition* w = melee ? melee->ChosenWeapon() : nullptr;
        CHECK(w != nullptr);
        if (w) CHECK_NEAR(w->damage, 33.0f, 1e-4);
        // A tree at the project root shadows the one next to the scene.
        fs::copy_file(assets / "behavior_trees" / "fail.okaybt", project / "succeed.okaybt", ec);
        BehaviorTree::Forget(std::string());
        const std::string near = (assets / "behavior_trees").string() + "/";
        CHECK(BehaviorTree::ResolvePath("succeed.okaybt", near) == (project / "succeed.okaybt").string());
        CHECK(BehaviorTree::ResolvePath("quick.okaybt", near) == (fs::path(near) / "quick.okaybt").string());
        // Nothing anywhere: the old answer (under the project folder) comes back.
        CHECK(BehaviorTree::ResolvePath("nope.okaybt", near) == (project / "nope.okaybt").string());
        // A sequence at the project folder shadows the scene's.
        SequencePlayer sp;
        sp.sequencePath = "intro.okayseq";
        sp.sceneDir = (assets / "cutscene").string() + "/";
        CHECK(fs::path(sp.ResolvedPath()) == assets / "cutscene" / "intro.okayseq");
        fs::copy_file(assets / "cutscene" / "intro.okayseq", project / "intro.okayseq", ec);
        CHECK(fs::path(sp.ResolvedPath()) == project / "intro.okayseq");
        sp.sceneDir.clear();
        fs::remove(project / "intro.okayseq", ec);
        CHECK(fs::path(sp.ResolvedPath()) == project / "intro.okayseq");   // unchanged without a scene folder
    }

    fs::current_path(cwd0, ec);
    BehaviorTree::Forget(std::string());
    WeaponBook::Get().Clear();
    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
