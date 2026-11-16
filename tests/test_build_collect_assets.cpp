// Build Game ships every file a scene's components open (issue #888, finding 41).
//   * SceneSerializer::CollectAssetPaths lists the dialogue, quest, weapon, clips, font and network-prefab files
//     (and the other component file fields), not only sprites, sounds and meshes;
//   * the build's dependency walk classes .okaydialog / .okayquest / .okayclip as data and reads them for the
//     files they use in turn (a dialogue's portraits and voice lines);
//   * the text safety net: a reference that only the scene's saved text names still ships, with one warning;
//   * a file written next to the scene (finding 28's case) ships, with what it names in turn;
//   * the example folders the Windows release ships give the safety net nothing to find.
// Temp folders only; nothing sleeps.
#include "test_framework.hpp"
#include "okay/Core/AssetDatabase.hpp"
#include "okay/Core/BuildPipeline.hpp"
#include "okay/Core/SaveFramework.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/AntiCheatConfig.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/BlockBuilder.hpp"
#include "okay/Components/BuilderMode.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/Crafting.hpp"
#include "okay/Components/CraftingStation.hpp"
#include "okay/Components/DialogueGraph.hpp"
#include "okay/Components/DialogueRunner.hpp"
#include "okay/Components/LoadingScreen.hpp"
#include "okay/Components/MeleeAttacker.hpp"
#include "okay/Components/Minimap.hpp"
#include "okay/Components/NetworkObject.hpp"
#include "okay/Components/NetworkPlayerSpawner.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/RangedWeapon.hpp"
#include "okay/Components/SequencePlayer.hpp"
#include "okay/Components/StructureBuilder.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/TopDownController2D.hpp"
#include "okay/Components/TopDownShooterController.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UITabs.hpp"
#include "okay/Components/UITheme.hpp"
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/Water.hpp"
#include "okay/Net/NetworkManager.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace okay;
namespace fs = std::filesystem;
namespace bp = okay::buildpipe;

namespace {

void Write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

bool Contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

bool Ships(const bp::WalkResult& w, const std::string& dest) {
    for (const auto& a : w.assets) if (a.dest == dest) return true;
    return false;
}

// The seven references finding 41 lists (nine fields: two weapons, three fonts), plus a SequencePlayer and an
// AudioSource as controls (those were collected before). Paths are written the way the Inspector writes them:
// relative to the project's Assets folder.
const char* const kFindingRefs[] = {
    "Dialogue/talk.okaydialog",  // DialogueRunner::graphFile
    "Quests/main.okayquest",     // QuestLog::questFile
    "Weapons/sword.okayweapon",  // MeleeAttacker::weaponFile
    "Weapons/gun.okayweapon",    // RangedWeapon::weaponFile
    "Anim/hero.okayanim",        // Character::clipsFile
    "Fonts/label.ttf",           // TextRenderer::fontPath
    "Fonts/button.ttf",          // UIButton::fontPath
    "Fonts/theme.otf",           // UITheme::fontPath
    "Prefabs/player.okayprefab", // NetworkPlayerSpawner::prefabFile
};
const char* const kControlRefs[] = {"Cutscenes/intro.okayseq", "Audio/hit.wav"};

void BuildFindingScene(Scene& s) {
    GameObject* npc = s.CreateGameObject("Guide");
    npc->AddComponent<DialogueRunner>()->graphFile = "Dialogue/talk.okaydialog";
    npc->AddComponent<QuestLog>()->questFile = "Quests/main.okayquest";
    GameObject* fighter = s.CreateGameObject("Fighter");
    fighter->AddComponent<MeleeAttacker>()->weaponFile = "Weapons/sword.okayweapon";
    fighter->AddComponent<RangedWeapon>()->weaponFile = "Weapons/gun.okayweapon";
    GameObject* hero = s.CreateGameObject("Hero");
    hero->AddComponent<Character>()->clipsFile = "Anim/hero.okayanim";
    GameObject* ui = s.CreateGameObject("UI");
    ui->AddComponent<TextRenderer>()->fontPath = "Fonts/label.ttf";
    ui->AddComponent<UIButton>()->fontPath = "Fonts/button.ttf";
    ui->AddComponent<UITheme>()->fontPath = "Fonts/theme.otf";
    GameObject* net = s.CreateGameObject("Spawner");
    net->AddComponent<NetworkPlayerSpawner>()->prefabFile = "Prefabs/player.okayprefab";
    GameObject* cut = s.CreateGameObject("Cutscene");
    cut->AddComponent<SequencePlayer>()->sequencePath = "Cutscenes/intro.okayseq";
    cut->AddComponent<AudioSource>()->clipPath = "Audio/hit.wav";
}

// Every reference in `refs` is found by ExtractSceneRefs in the scene's saved text: the safety net reads the same
// paths the component walk lists, so a collected file is never also reported as "found only by the text scan".
void CheckTextParity(const Scene& s, const std::vector<std::string>& refs) {
    std::set<std::string> found;
    for (const FoundRef& r : ExtractSceneRefs(SceneSerializer::Serialize(s))) found.insert(r.path);
    for (const std::string& p : refs) {
        const bool ok = found.count(p) != 0;
        if (!ok) std::cerr << "  (the scene text scan does not see " << p << ")\n";
        CHECK(ok);
    }
}

void TestCollect() {
    RUN_SUITE("CollectAssetPaths: the files finding 41 found missing");
    Scene s("collect");
    BuildFindingScene(s);
    const std::vector<std::string> paths = SceneSerializer::CollectAssetPaths(s);
    int have = 0;
    for (const char* p : kFindingRefs) {
        const bool ok = Contains(paths, p);
        if (!ok) std::cerr << "  (not collected: " << p << ")\n";
        CHECK(ok);
        have += ok ? 1 : 0;
    }
    for (const char* p : kControlRefs) CHECK(Contains(paths, p));
    std::cout << "  collected " << have << " of " << (sizeof(kFindingRefs) / sizeof(kFindingRefs[0])) << " finding refs, "
              << paths.size() << " paths in all\n";
    CHECK(paths.size() == sizeof(kFindingRefs) / sizeof(kFindingRefs[0]) + sizeof(kControlRefs) / sizeof(kControlRefs[0]));
    CheckTextParity(s, paths);

    // Empty fields add nothing.
    Scene e("empty");
    GameObject* o = e.CreateGameObject("O");
    o->AddComponent<DialogueRunner>(); o->AddComponent<QuestLog>(); o->AddComponent<MeleeAttacker>(); o->AddComponent<RangedWeapon>();
    o->AddComponent<Character>(); o->AddComponent<TextRenderer>(); o->AddComponent<UIButton>(); o->AddComponent<UITheme>();
    o->AddComponent<NetworkPlayerSpawner>();
    CHECK(SceneSerializer::CollectAssetPaths(e).empty());
}

void TestCollectOthers() {
    RUN_SUITE("CollectAssetPaths: the other component file fields");
    Scene s("others");
    GameObject* net = s.CreateGameObject("Net");
    net->AddComponent<NetworkManager>()->spawnablePrefabs = {"Prefabs/crate.okayprefab", "Prefabs/barrel.okayprefab"};
    auto* ac = net->AddComponent<AntiCheatConfig>();
    ac->file = "Rules/server.okayac";
    ac->settings.spawnAllowlist = {"Prefabs/rocket.okayprefab"};
    GameObject* load = s.CreateGameObject("Loading");
    auto* ls = load->AddComponent<LoadingScreen>();
    ls->logoImage = "UI/logo.png";
    ls->backgroundImage = "UI/back.jpg";
    GameObject* map = s.CreateGameObject("Map");
    map->AddComponent<Minimap>()->mapTexture = "UI/world.png";
    map->AddComponent<MinimapBlip>()->icon = "UI/blip.png";
    GameObject* world = s.CreateGameObject("World");
    world->AddComponent<Water>()->texture = "Textures/ripples.png";
    world->AddComponent<VoxelTerrain>()->texture = "Textures/ground.png";
    GameObject* build = s.CreateGameObject("Builder");
    build->AddComponent<BlockBuilder>()->blockTexture = "Textures/block.png";
    build->AddComponent<StructureBuilder>()->structureTexture = "Textures/wall.png";
    auto* bm = build->AddComponent<BuilderMode>();
    bm->partTexture = "Textures/part.png";
    bm->prefabPath = "Prefabs/stamp.okayprefab";
    GameObject* craft = s.CreateGameObject("Craft");
    craft->AddComponent<Crafting>()->recipeBook = "Data/hand.okayrecipes";
    craft->AddComponent<CraftingStation>()->recipeBook = "Data/forge.okayrecipes";
    GameObject* shooter = s.CreateGameObject("Shooter");
    shooter->AddComponent<TopDownController2D>()->projectile = "Prefabs/bullet2d.okayprefab";
    shooter->AddComponent<TopDownShooterController>()->projectile = "Prefabs/bullet.okayprefab";
    shooter->AddComponent<RangedWeapon>()->projectile = "Prefabs/rocket_shot.okayprefab";
    GameObject* tabs = s.CreateGameObject("Tabs");
    tabs->AddComponent<UITabs>()->icons = {"UI/tab_a.png", "", "UI/tab_c.png"};
    GameObject* save = s.CreateGameObject("Saved");
    save->AddComponent<Saveable>()->prefab = "Prefabs/saved.okayprefab";
    GameObject* fac = s.CreateGameObject("Factory");
    fac->AddComponent<Saveable>()->prefab = "factory:Crate";   // made in code: no file
    GameObject* dyn = s.CreateGameObject("Dynamic");
    dyn->prefabSource = "Prefabs/dynamic.okayprefab";
    dyn->AddComponent<Saveable>()->dynamic = true;             // respawned from its own prefab on load
    GameObject* inst = s.CreateGameObject("Instance");
    inst->prefabSource = "Prefabs/link_only.okayprefab";        // an editor link: the instance's content is in the scene

    const std::vector<std::string> expected = {
        "Prefabs/crate.okayprefab", "Prefabs/barrel.okayprefab", "Rules/server.okayac", "Prefabs/rocket.okayprefab",
        "UI/logo.png", "UI/back.jpg", "UI/world.png", "UI/blip.png", "Textures/ripples.png", "Textures/ground.png",
        "Textures/block.png", "Textures/wall.png", "Textures/part.png", "Prefabs/stamp.okayprefab",
        "Data/hand.okayrecipes", "Data/forge.okayrecipes", "Prefabs/bullet2d.okayprefab", "Prefabs/bullet.okayprefab",
        "Prefabs/rocket_shot.okayprefab", "UI/tab_a.png", "UI/tab_c.png", "Prefabs/saved.okayprefab", "Prefabs/dynamic.okayprefab"};
    const std::vector<std::string> paths = SceneSerializer::CollectAssetPaths(s);
    int have = 0;
    for (const std::string& p : expected) {
        const bool ok = Contains(paths, p);
        if (!ok) std::cerr << "  (not collected: " << p << ")\n";
        CHECK(ok);
        have += ok ? 1 : 0;
    }
    std::cout << "  collected " << have << " of " << expected.size() << " other component refs\n";
    CHECK(!Contains(paths, "factory:Crate"));
    CHECK(!Contains(paths, "Prefabs/link_only.okayprefab"));
    CHECK(paths.size() == expected.size());
    CheckTextParity(s, paths);
}

void TestKinds() {
    RUN_SUITE("file kinds: dialogue, quest and clip files are data");
    CHECK(bp::KindOf("Game_Data/Dialogue/talk.okaydialog") == bp::FileKind::Data);
    CHECK(bp::KindOf("Game_Data/Quests/main.okayquest") == bp::FileKind::Data);
    CHECK(bp::KindOf("Game_Data/Anim/wave.okayclip") == bp::FileKind::Data);
    CHECK(bp::KindOf("Game_Data/Data/forge.okayrecipes") == bp::FileKind::Data);
    CHECK(bp::KindOf("Game_Data/Data/chest.okayloot") == bp::FileKind::Data);
    CHECK(bp::KindOf("Game_Data/Anim/hero.okayanim") == bp::FileKind::Data);   // unchanged
}

// The project finding 41 describes: every file on disk, the scene saved in Assets/Levels, a dialogue with a portrait
// and a voice line. Returns the scene file.
fs::path MakeProject(const fs::path& proj) {
    const fs::path assets = proj / "Assets";
    DialogueGraph g;
    g.name = "Talk";
    g.start = 0;
    g.speakers = {"Smith"};
    DialogueNode line;
    line.id = 0; line.kind = DialogueNode::Kind::Line; line.speaker = "Smith"; line.text = "Hello there";
    line.portrait = "Portraits/smith.png"; line.sound = "Audio/hello.wav"; line.next = 1;
    DialogueNode end;
    end.id = 1; end.kind = DialogueNode::Kind::End; end.x = 200;
    g.nodes = {line, end};
    Write(assets / "Dialogue" / "talk.okaydialog", g.Write());
    Write(assets / "Portraits" / "smith.png", std::string(64, 'p'));
    Write(assets / "Audio" / "hello.wav", std::string(48, 'h'));
    Write(assets / "Quests" / "main.okayquest", "okayquest 1\nquest id=\"main\" title=\"Find the key\"\nobjective kind=\"talk\" target=\"Guide\" count=1\n");
    Write(assets / "Weapons" / "sword.okayweapon", "okayweapon 1\n");
    Write(assets / "Weapons" / "gun.okayweapon", "okayweapon 1\n");
    Write(assets / "Anim" / "hero.okayanim", "clip wave 1\n");
    Write(assets / "Fonts" / "label.ttf", std::string(40, 'f'));
    Write(assets / "Fonts" / "button.ttf", std::string(41, 'f'));
    Write(assets / "Fonts" / "theme.otf", std::string(42, 'f'));
    Write(assets / "Prefabs" / "player.okayprefab", "okayscene 3\ngameobject 0 \"Player\"\nend\n");
    Write(assets / "Cutscenes" / "intro.okayseq", "okayseq 1\n");
    Write(assets / "Audio" / "hit.wav", std::string(50, 'w'));
    Write(assets / "Unused" / "big.png", std::string(70, 'u'));
    Scene s("level");
    BuildFindingScene(s);
    const fs::path scene = assets / "Levels" / "level.okayscene";
    fs::create_directories(scene.parent_path());
    CHECK(SceneSerializer::SaveToFile(s, scene.string()));
    return scene;
}

// What Build Game does for one scene copied as a file, with asset stripping on (the default): the scene's
// component walk gives the roots, the dependency walk follows them, then the text safety net reads the scene's saved
// text. Warnings are counted the way the build report counts them: one per missing file, one per absolute path and
// one per file only the text scan found.
struct MiniBuild { bp::WalkResult walk; std::vector<bp::TextScanFind> finds; int warnings = 0; };
MiniBuild StrippedBuild(const fs::path& proj, const fs::path& sceneFile) {
    MiniBuild b;
    Scene loaded("build");
    std::string err;
    CHECK(SceneSerializer::LoadFromFile(loaded, sceneFile.string(), &err));
    std::vector<bp::AssetRef> roots;
    for (const std::string& a : SceneSerializer::CollectAssetPaths(loaded)) roots.push_back({a, sceneFile.generic_string()});
    bp::WalkSettings ws;
    ws.projectDir = proj.generic_string();
    ws.searchDirs.push_back(sceneFile.parent_path().generic_string());
    b.walk = bp::WalkAssets(roots, ws);
    std::ifstream in(sceneFile, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    b.finds = bp::AddTextScanFinds({{text.str(), sceneFile.generic_string()}}, {sceneFile.generic_string()}, ws, b.walk);
    b.warnings = (int)(b.walk.missing.size() + b.walk.absolute.size() + b.finds.size());
    for (const auto& m : b.walk.missing) std::cerr << "  (missing: " << m.ref << ")\n";
    return b;
}

void TestPipeline(const fs::path& tmp) {
    RUN_SUITE("stripped build: the dialogue ships, and so does its portrait");
    const fs::path proj = tmp / "proj";
    const fs::path scene = MakeProject(proj);
    const MiniBuild b = StrippedBuild(proj, scene);
    for (const char* p : kFindingRefs) {
        const bool ok = Ships(b.walk, p);
        if (!ok) std::cerr << "  (not shipped: " << p << ")\n";
        CHECK(ok);
    }
    for (const char* p : kControlRefs) CHECK(Ships(b.walk, p));
    CHECK(Ships(b.walk, "Portraits/smith.png"));   // named inside the dialogue
    CHECK(Ships(b.walk, "Audio/hello.wav"));
    CHECK(!Ships(b.walk, "Unused/big.png"));
    std::string portraitReason;
    for (const auto& a : b.walk.assets) if (a.dest == "Portraits/smith.png") portraitReason = a.reason;
    CHECK(portraitReason == "used by talk.okaydialog");
    CHECK(b.finds.empty());   // every reference was listed by a component: the safety net adds nothing
    CHECK(b.warnings == 0);
    std::cout << "  shipped " << b.walk.assets.size() << " files, " << b.warnings << " warnings\n";
}

void TestSafetyNet(const fs::path& tmp) {
    RUN_SUITE("safety net: a file only the scene text names ships, with one warning");
    const fs::path proj = tmp / "net";
    const fs::path assets = proj / "Assets";
    Write(assets / "Skins" / "red.png", std::string(30, 'r'));
    Write(assets / "Dialogue" / "talk.okaydialog", "okaydialog 1\nname \"T\"\nstart 0\nnode id=0 kind=\"end\" x=0 y=0\n");
    Write(assets / "Prefabs" / "crate.okayprefab", "okayscene 3\ngameobject 0 \"Crate\"\nend\n");   // only an instance's link names it
    Write(assets / "Levels" / "two.okayscene", "okayscene 3\n");                               // a scene: ships through Scenes In Build
    Scene s("net");
    GameObject* hero = s.CreateGameObject("Hero");
    hero->AddComponent<DialogueRunner>()->graphFile = "Dialogue/talk.okaydialog";   // listed by the component walk: no warning
    auto* skin = hero->AddComponent<SyncVar>();                                      // a custom value: no component walk knows it
    skin->name = "skin";
    skin->value = "Skins/red.png";
    GameObject* inst = s.CreateGameObject("Crate");
    inst->prefabSource = "Prefabs/crate.okayprefab";
    s.CreateGameObject("Loader")->AddComponent<LoadingScreen>()->targetScene = "Levels/two.okayscene";
    const fs::path scene = assets / "Levels" / "level.okayscene";
    CHECK(SceneSerializer::SaveToFile(s, scene.string()));

    const MiniBuild b = StrippedBuild(proj, scene);
    CHECK(Ships(b.walk, "Dialogue/talk.okaydialog"));
    CHECK(Ships(b.walk, "Skins/red.png"));                       // shipped by the safety net
    CHECK(!Ships(b.walk, "Prefabs/crate.okayprefab"));           // the editor's link to the prefab is not a file the game opens
    CHECK(!Ships(b.walk, "Levels/two.okayscene"));
    CHECK(b.finds.size() == 1);
    CHECK(b.walk.missing.empty() && b.walk.absolute.empty());
    CHECK(b.warnings == 1);
    if (b.finds.size() == 1) {
        const bp::TextScanFind& f = b.finds[0];
        CHECK(f.ref == "Skins/red.png" && f.record == "syncvar" && f.object == "Hero" && f.field.empty());
        const std::string w = bp::TextScanWarning(f);
        std::cout << "  warning: " << w << "\n";
        CHECK(w.rfind("Shipped 'Skins/red.png', which only the scene-text scan found", 0) == 0);
        CHECK(w.find("scene level.okayscene") != std::string::npos && w.find("object 'Hero'") != std::string::npos &&
              w.find("record 'syncvar'") != std::string::npos);
    }

    // A text-only reference to a dialogue ships the dialogue AND what it names (one warning: the dialogue).
    Write(assets / "Dialogue" / "extra.okaydialog",
          "okaydialog 1\nname \"E\"\nstart 0\nnode id=0 kind=\"line\" x=0 y=0 speaker=\"A\" text=\"Hi\" portrait=\"Portraits/a.png\" next=-1\n");
    Write(assets / "Portraits" / "a.png", std::string(20, 'a'));
    skin->value = "Dialogue/extra.okaydialog";
    // A text-only reference to nothing: a missing asset, not a text-scan find.
    auto* ghost = hero->AddComponent<SyncVar>();
    ghost->name = "ghost";
    ghost->value = "Skins/none.png";
    CHECK(SceneSerializer::SaveToFile(s, scene.string()));
    const MiniBuild c = StrippedBuild(proj, scene);
    CHECK(Ships(c.walk, "Dialogue/extra.okaydialog") && Ships(c.walk, "Portraits/a.png"));
    CHECK(!Ships(c.walk, "Skins/red.png"));
    CHECK(c.finds.size() == 1 && !c.finds.empty() && c.finds[0].ref == "Dialogue/extra.okaydialog");
    CHECK(c.walk.missing.size() == 1 && !c.walk.missing.empty() && c.walk.missing[0].ref == "Skins/none.png");
    CHECK(c.warnings == 2);   // one shipped by the text scan, one missing

    // The scene written from the editor (the open scene) reads the same: its graphs are accounted for, not re-shipped.
    std::vector<bp::SceneText> texts = {{SceneSerializer::Serialize(s), scene.generic_string()}};
    bp::WalkSettings ws;
    ws.projectDir = proj.generic_string();
    bp::WalkResult w = bp::WalkAssets({}, ws);
    const std::vector<bp::TextScanFind> again = bp::AddTextScanFinds(texts, {scene.generic_string()}, ws, w);
    CHECK(again.size() == 2);   // the dialogue (DialogueRunner is not a root in this call) and the extra dialogue
    CHECK(Ships(w, "Portraits/a.png"));
}

// The example folders the Windows release ships (scripts/build-windows-dist.sh), each built as its own project: every
// file their scenes name is listed by a component, so the safety net finds nothing and adds no warning.
void TestShippedExamples() {
    RUN_SUITE("shipped examples: the safety net finds nothing extra");
    const fs::path root = fs::path(OKAY_SOURCE_DIR) / "examples";
    int scenes = 0, finds = 0, missing = 0;
    for (const char* dir : {"vehicle", "own_wav", "own_readers", "obj_texture_paths", "spatial_audio", "cutscene", "behavior_trees",
                            "node_playground", "physics", "physics2d", "weapons", "touch_controls"}) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(root / dir, ec)) {
            if (e.path().extension() != ".okayscene") continue;
            ++scenes;
            const MiniBuild b = StrippedBuild(root / dir, e.path());
            for (const auto& f : b.finds) { ++finds; std::cerr << "  (" << dir << "/" << e.path().filename().string() << ": " << bp::TextScanWarning(f) << ")\n"; }
            missing += (int)b.walk.missing.size();
        }
    }
    std::cout << "  " << scenes << " example scenes: " << finds << " text-scan finds, " << missing << " missing references\n";
    CHECK(scenes > 10);
    CHECK(finds == 0);
}

// Paths written relative to the scene's own folder (finding 28): a scene in Assets/<folder>/ that names a sibling file. The
// component walk lists the name as written and the build's walk finds it next to the scene file (the referrer's folder),
// so it ships whether or not the components themselves look next to the scene; what that file names (a dialogue's
// portrait, a behavior tree's sub-trees) is looked for next to that file in turn.
void TestSceneRelative(const fs::path& tmp) {
    RUN_SUITE("scene-relative paths: a file next to the scene ships, with what it names");
    const fs::path proj = tmp / "rel";
    const fs::path level = proj / "Assets" / "Levels";
    Write(level / "near.okaydialog",
          "okaydialog 1\nname \"N\"\nstart 0\nnode id=0 kind=\"line\" x=0 y=0 speaker=\"A\" text=\"Hi\" portrait=\"near.png\" next=-1\n");
    Write(level / "near.png", std::string(25, 'n'));
    Write(level / "axe.okayweapon", "okayweapon 1\n");
    Scene s("rel");
    GameObject* npc = s.CreateGameObject("NPC");
    npc->AddComponent<DialogueRunner>()->graphFile = "near.okaydialog";
    npc->AddComponent<MeleeAttacker>()->weaponFile = "axe.okayweapon";
    const fs::path scene = level / "arena.okayscene";
    CHECK(SceneSerializer::SaveToFile(s, scene.string()));
    const MiniBuild b = StrippedBuild(proj, scene);
    CHECK(Ships(b.walk, "near.okaydialog") && Ships(b.walk, "near.png") && Ships(b.walk, "axe.okayweapon"));
    CHECK(b.warnings == 0);

    // The behavior_trees example copied into a project's Assets/ (finding 28's case): GuardBrain.okaybt runs greet.okaybt,
    // which only the tree names. Both ship.
    const fs::path bt = proj / "Assets" / "behavior_trees";
    std::error_code ec;
    fs::create_directories(bt, ec);
    for (const auto& e : fs::directory_iterator(fs::path(OKAY_SOURCE_DIR) / "examples" / "behavior_trees", ec))
        if (e.is_regular_file()) fs::copy_file(e.path(), bt / e.path().filename(), fs::copy_options::overwrite_existing, ec);
    const MiniBuild t = StrippedBuild(proj, bt / "subtree_demo.okayscene");
    CHECK(Ships(t.walk, "GuardBrain.okaybt"));
    CHECK(Ships(t.walk, "greet.okaybt"));   // a sub-tree: named by GuardBrain.okaybt, not by the scene
    CHECK(t.finds.empty() && t.walk.missing.empty());
    std::cout << "  behavior_trees copied into Assets/: " << t.walk.assets.size() << " files shipped, " << t.warnings << " warnings\n";
}

} // namespace

int main() {
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec) / ("okay_build_collect_" + std::to_string((long long)std::time(nullptr)));
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    TestCollect();
    TestCollectOthers();
    TestKinds();
    TestPipeline(tmp);
    TestSafetyNet(tmp);
    TestSceneRelative(tmp);
    TestShippedExamples();
    fs::remove_all(tmp, ec);
    TEST_MAIN_RESULT();
}
