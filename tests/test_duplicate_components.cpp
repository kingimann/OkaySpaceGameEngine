// Two component instances on one object survive save/load, a prefab apply, an undo
// delta, a scene diff and merge, a copy, and asset export. Old example scenes still
// load, and saving them does not drop a component they had.
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include <Okay.hpp>
#include "okay/Components/LODGroup.hpp"
#include "okay/Components/Occluder.hpp"
#include "okay/Components/SettingsScreen.hpp"
#include "okay/Testing/PlayTest.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/PrefabTools.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/SceneDiff.hpp"
#include "okay/Scene/SceneMerge.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

using namespace okay;

#define OKAY_SAVE_COMPONENTS(X) \
    X(ActionList) X(AimIK) X(AimOffset) X(AnimStateMachine) X(Animator) X(AntiCheatConfig) X(AudioSource) \
    X(AutoSave) X(BehaviorTreeRunner) X(BleedingStat) X(BlendShapes) X(BlockBuilder) X(BoxCollider2D) \
    X(BoxCollider3D) X(BuildMode) X(BuildPiece) X(Buildable) X(BuilderMode) X(Cable) X(Camera) X(CameraFollow) X(Canvas) \
    X(CapsuleCollider2D) X(CapsuleCollider3D) X(CarryWeightStat) X(ChainIK) X(Character) X(CharacterController2D) \
    X(CharacterController3D) X(CheckpointTrigger) X(ChestInventory) X(CinemachineBrain) X(CircleCollider2D) \
    X(ClickToMoveController) X(Collectible) X(Consumables) X(Crafting) X(CraftingMenu) X(CraftingStation) \
    X(Crosshair) X(CylinderCollider3D) X(DamageOnTouch) X(DayNightCycle) X(Decal) X(Destructible) X(DialogueRunner) \
    X(DollyCart) X(DollyPath) X(Draggable) X(DropZone) X(EdgeCollider2D) X(Equipment) X(testing::EventProbe) \
    X(EventSystem) X(FirstPersonController) X(FirstPersonHand) X(FirstPersonHands) X(Flashlight) \
    X(FollowTarget2D) X(Foliage) X(FootIK) X(FreeRoamController) X(FullBodyIK) X(GridInventory) X(GridInventoryUI) \
    X(HealthStat) X(HoldPrompt) X(HoldableObject) X(HumanoidRetarget) X(HungerStat) X(IKTarget) X(Inventory) \
    X(InventoryUI) X(ItemPickup) X(Joint2D) X(Joint3D) X(JumpPad) X(Lifetime) X(Light) X(LimbIK) X(LineRenderer) \
    X(LoadingScreen) X(LogicSource) X(LogicTarget) X(LookAtIK) X(LootSource) X(MeleeAttacker) X(MeshCollider3D) \
    X(MeshModifiers) X(MeshRenderer) X(Minimap) X(MinimapBlip) X(ModelAnimator) X(Mover) X(MusicPlayer) \
    X(NPCController) X(NavMeshAgent) X(NavMeshLink) X(NavMeshModifier) X(NavMeshObstacle) X(NavMeshSurface) X(NetInventory) X(NetValidator) \
    X(NetworkAvatarMotion) X(NetworkManager) X(NetworkObject) X(NetworkPlayerSpawner) X(NetworkSync) \
    X(NetworkTransform) X(OxygenStat) X(ParticleSystem) X(PauseMenu) X(Platformer25DController) \
    X(PlayerController) X(PlayerView) X(PoisonStat) X(PolygonCollider2D) X(PoseCapture) \
    X(ProceduralObjectHolding) X(Projectile3D) X(QuestLog) X(QuestTrigger) X(RadiationStat) X(Ragdoll) \
    X(Rigidbody2D) X(Rigidbody3D) X(RootMotion) X(SanityStat) X(SaveConfig) X(Saveable) X(ScriptComponent) X(Seat) \
    X(SequencePlayer) X(SettingsScreen) X(SkinnedMesh) X(SleepStat) X(Spawner) X(SphereCollider3D) X(Spinner) \
    X(Spline) X(SplineFollower) X(SplineInstancer) X(SplineMesh) X(SpriteAnimator) X(SpriteRenderer) \
    X(StaminaStat) X(StatComponent) X(Stats) X(StatusEffectStat) X(SteamManager) X(StoryState) \
    X(StructureBuilder) X(SurvivalSave) X(SurvivalStats) X(SurvivalZone) X(SyncVar) X(Teleporter) \
    X(TemperatureStat) X(Terrain) X(TerrainDigger) X(TextRenderer) X(ThirdPersonController) \
    X(ThirdPersonShooterController) X(ThirstStat) X(Tilemap) X(TilemapCollider2D) X(TimedDestroy) \
    X(TopDownController) X(TopDownController2D) X(TopDownShooterController) X(TrailRenderer) X(TriggerZone) \
    X(TurnManager) X(TweenAnimation) X(UIAnimation) X(UIBarBind) X(UIButton) X(UIColorPicker) X(UIContextMenu) \
    X(UIDamageNumbers) X(UIDialogueBox) X(UIDocument) X(UIDraggable) X(UIDropTarget) X(UIDropdown) X(UIFlexItem) X(UIFrame) X(UIImage) \
    X(UIInputField) X(UIInventoryBind) X(UIKeybindField) X(UILayoutGroup) X(UIListView) X(UIMask) X(UIModal) X(UIPanel) \
    X(UIProgressBar) X(UIRadialProgress) X(UIRating) X(UIRawImage) X(UISafeArea) X(UIScrollView) X(UISlider) \
    X(UISlotGrid) X(UIStepper) X(UIStyleVariant) X(UITabs) X(UITextBind) X(UITheme) X(UIToast) X(UIToastItem) X(UIToggle) \
    X(UITooltip) X(VehicleController) X(VehicleController2D) X(VirtualCamera) X(VisualScriptComponent) \
    X(VoxelDigger) X(VoxelTerrain) X(Volume) X(Water) X(WeatherRig) X(WetnessStat) X(WorldSpaceUI) X(WorldStreamer) \
    X(WorldUI) X(LODGroup) X(Occluder)

namespace {

const std::set<std::string> kRuntimeOnly = {
    "StatComponent", "TimedDestroy", "UIToastItem", "WeatherRig", "NetworkAvatarMotion",
    "testing::EventProbe", "SettingsScreen", "SteamManager", "CheckpointTrigger"};

// These adopt or rebuild something on the first load, then the text settles.
const std::set<std::string> kChangesOnce = {
    "FirstPersonController", "ThirdPersonController", "PlayerController", "PlayerView", "VoxelTerrain", "Water"};

std::map<std::string, int> TypeCounts(const Scene& scene) {
    std::map<std::string, int> counts;
    for (const auto& go : scene.Objects())
        for (const auto& c : go->Components())
            if (c && c.get() != static_cast<const Component*>(go->transform))
                ++counts[ComponentSerializerRegistry::TypeKey(*c)];
    return counts;
}

void ShowDiff(const std::string& a, const std::string& b) {
    std::istringstream x(a), y(b);
    std::string lx, ly;
    while (std::getline(x, lx) && std::getline(y, ly))
        if (lx != ly) {
            std::cerr << "      saved:  " << lx.substr(0, 160) << "\n      resaved:" << ly.substr(0, 160) << "\n";
            return;
        }
    if (std::getline(x, lx)) std::cerr << "      saved only: " << lx.substr(0, 160) << "\n";
    if (std::getline(y, ly)) std::cerr << "      resaved only: " << ly.substr(0, 160) << "\n";
}

PropertyValue Sample(PropertyType type, int which) {
    switch (type) {
    case PropertyType::Bool: return PropertyValue::MakeBool(which != 0);
    case PropertyType::Int: return PropertyValue::MakeInt(which == 0 ? 3 : 9);
    case PropertyType::Float: return PropertyValue::MakeFloat(which == 0 ? 1.25f : 7.5f);
    case PropertyType::String: return PropertyValue::MakeString(which == 0 ? "alpha" : "beta");
    default: return {};
    }
}

// Set one authored property differently on each instance, when the registry knows one.
std::string Distinguish(Component& first, Component& second) {
    auto info = PropertyRegistry::FindFor(first);
    if (!info) return {};
    for (const PropertyDesc& p : info->properties) {
        if (p.name == "enabled" || p.name.find("Enabled") != std::string::npos || p.name.find("enabled") != std::string::npos)
            continue;
        if (p.IsReadOnly() || (p.flags & PropertyFlag::RuntimeOnly) || (p.flags & PropertyFlag::Derived)) continue;
        PropertyValue a = Sample(p.type, 0), b = Sample(p.type, 1);
        if (!a.IsValid()) continue;
        std::string err;
        if (!PropertyRegistry::SetComponentProperty(first, p.name, a, &err)) continue;
        if (!PropertyRegistry::SetComponentProperty(second, p.name, b, &err)) continue;
        return p.name;
    }
    return {};
}

template <class T>
void Prepare(T* first, T* second) {
    if constexpr (std::is_same<T, LODGroup>::value) {
        first->levels.push_back({});
        second->levels.push_back({});
        first->levels[0].distance = 4.0f;
        second->levels[0].distance = 12.0f;
    }
}

template <class T>
void RoundTripTwo(const char* name, std::vector<std::string>& failed) {
    Scene a("a");
    GameObject* g = a.CreateGameObject("O");
    T* first = g->AddComponent<T>();
    T* second = g->AddComponent<T>();
    if (!first || !second || first == second) {
        std::cerr << "    " << name << ": could not add two instances\n";
        failed.push_back(name);
        CHECK(false);
        return;
    }
    second->enabled = false;
    Prepare(first, second);
    const std::string prop = Distinguish(*first, *second);
    PropertyValue want0, want1;
    if (!prop.empty()) {
        want0 = PropertyRegistry::GetComponentProperty(*first, prop);
        want1 = PropertyRegistry::GetComponentProperty(*second, prop);
    }

    const std::string text1 = SceneSerializer::Serialize(a);
    Scene b("b");
    std::string err;
    std::vector<std::string> warnings;
    if (!SceneSerializer::Deserialize(b, text1, &err, &warnings)) {
        std::cerr << "    " << name << ": did not load: " << err << "\n";
        failed.push_back(name);
        CHECK(false);
        return;
    }
    for (const auto& w : warnings)
        if (w.find("only the first is saved") != std::string::npos) {
            std::cerr << "    " << name << ": " << w << "\n";
            failed.push_back(name);
            CHECK(false);
        }
    GameObject* back = b.Find("O");
    auto comps = back ? back->GetComponents<T>() : std::vector<T*>{};
    if (comps.size() != 2 || !comps[0]->enabled || comps[1]->enabled) {
        std::cerr << "    " << name << ": " << comps.size() << " after load (wanted 2, second switched off)\n";
        failed.push_back(name);
        CHECK(comps.size() == 2);
        return;
    }
    if (!prop.empty()) {
        const PropertyValue got0 = PropertyRegistry::GetComponentProperty(*comps[0], prop);
        const PropertyValue got1 = PropertyRegistry::GetComponentProperty(*comps[1], prop);
        if (got0 != want0 || got1 != want1) {
            std::cerr << "    " << name << "." << prop << " came back " << got0.ToString() << " / " << got1.ToString()
                      << " (wanted " << want0.ToString() << " / " << want1.ToString() << ")\n";
            failed.push_back(name);
            CHECK(false);
        }
    }
    const std::string text2 = SceneSerializer::Serialize(b);
    Scene c("c");
    if (!SceneSerializer::Deserialize(c, text2, &err, nullptr)) {
        std::cerr << "    " << name << ": resave did not load: " << err << "\n";
        failed.push_back(name);
        CHECK(false);
        return;
    }
    const std::string text3 = SceneSerializer::Serialize(c);
    if (text2 != text3) {
        std::cerr << "    " << name << " is not stable on the second save\n";
        ShowDiff(text2, text3);
        failed.push_back(name);
        CHECK(false);
    } else if (text1 != text2 && !kChangesOnce.count(name)) {
        std::cerr << "    " << name << " changed on the first load (a single instance must not)\n";
        ShowDiff(text1, text2);
        failed.push_back(name);
        CHECK(false);
    }
    auto again = c.Find("O") ? c.Find("O")->GetComponents<T>() : std::vector<T*>{};
    CHECK(again.size() == 2 && again[0]->enabled && !again[1]->enabled);
}

void TestEveryType() {
    RUN_SUITE("two instances of every serializable component");
    Log::console = false;
    RegisterBuiltinProperties();
    std::vector<std::string> failed, dropped;
    int total = 0;
#define X(T)                                          \
    {                                                 \
        ++total;                                      \
        if (kRuntimeOnly.count(#T)) {                 \
            Scene s("s");                             \
            GameObject* g = s.CreateGameObject("O");  \
            g->AddComponent<T>();                     \
            Scene t("t");                             \
            SceneSerializer::Deserialize(t, SceneSerializer::Serialize(s)); \
            if (t.Find("O") && t.Find("O")->GetComponent<T>()) dropped.push_back(#T); \
        } else {                                      \
            RoundTripTwo<T>(#T, failed);              \
        }                                             \
    }
    OKAY_SAVE_COMPONENTS(X)
#undef X
    for (const auto& n : dropped) std::cerr << "    runtime-only type is saved now: " << n << "\n";
    CHECK(dropped.empty());
    CHECK(failed.empty());
    CHECK(total >= 217);
    std::cout << "  " << total << " types, " << failed.size() << " failed\n";
}

void TestOrder() {
    RUN_SUITE("duplicate components keep their order around other types");
    Scene a("a");
    GameObject* o = a.CreateGameObject("O");
    o->AddComponent<SpriteRenderer>()->glyph = 'A';
    o->AddComponent<Camera>()->depth = 3.0f;
    auto* second = o->AddComponent<SpriteRenderer>();
    second->glyph = 'B';
    second->enabled = false;
    Scene b("b");
    CHECK(SceneSerializer::Deserialize(b, SceneSerializer::Serialize(a)));
    GameObject* r = b.Find("O");
    CHECK(r != nullptr);
    if (!r) return;
    std::vector<std::string> keys;
    for (const auto& c : r->Components())
        if (c.get() != static_cast<Component*>(r->transform))
            keys.push_back(ComponentSerializerRegistry::TypeKey(*c));
    CHECK(keys.size() == 3);
    if (keys.size() == 3) {
        CHECK(keys[0] == "SpriteRenderer" && keys[1] == "Camera" && keys[2] == "SpriteRenderer");
    }
    auto sprites = r->GetComponents<SpriteRenderer>();
    CHECK(sprites.size() == 2 && sprites[0]->glyph == 'A' && sprites[0]->enabled);
    CHECK(sprites.size() == 2 && sprites[1]->glyph == 'B' && !sprites[1]->enabled);
    CHECK(r->GetComponent<Camera>() && r->GetComponent<Camera>()->depth == 3.0f);
    CHECK(SceneSerializer::Serialize(b) == SceneSerializer::Serialize(a));
}

void TestPrefabApply() {
    RUN_SUITE("prefab apply keeps both instances");
    Scene src("src");
    GameObject* o = src.CreateGameObject("Lamp");
    auto* a = o->AddComponent<Light>();
    a->intensity = 1.0f;
    a->type = Light::Type::Point;
    auto* b = o->AddComponent<Light>();
    b->intensity = 4.0f;
    b->type = Light::Type::Spot;
    b->enabled = false;
    const std::string path = "lamp.okayprefab";
    std::string err;
    CHECK(PrefabTools::SavePrefab(*o, path, &err));
    Scene host("host");
    GameObject* inst = SceneSerializer::InstantiateFromFile(host, path, &err);
    CHECK(inst != nullptr);
    if (!inst) { std::cerr << "    " << err << "\n"; return; }
    auto lights = inst->GetComponents<Light>();
    CHECK(lights.size() == 2 && lights[0]->intensity == 1.0f && lights[1]->intensity == 4.0f && !lights[1]->enabled);
    lights[1]->intensity = 8.0f;
    lights[1]->enabled = true;
    CHECK(PrefabTools::ApplyAllToSource(*inst, &err));
    Scene again("again");
    GameObject* fresh = SceneSerializer::InstantiateFromFile(again, path, &err);
    CHECK(fresh != nullptr);
    if (!fresh) return;
    auto got = fresh->GetComponents<Light>();
    CHECK(got.size() == 2 && got[0]->intensity == 1.0f && got[0]->type == Light::Type::Point);
    CHECK(got.size() == 2 && got[1]->intensity == 8.0f && got[1]->type == Light::Type::Spot && got[1]->enabled);
}

void TestUndoDiffMergeCopyExport() {
    RUN_SUITE("undo delta, diff, merge, copy and export");
    Scene a("a");
    GameObject* o = a.CreateGameObject("O");
    auto* s0 = o->AddComponent<SpriteRenderer>();
    s0->glyph = 'A';
    s0->color = Color(1, 0, 0, 1);
    auto* s1 = o->AddComponent<SpriteRenderer>();
    s1->glyph = 'B';
    s1->color = Color(0, 1, 0, 1);
    o->AddComponent<MeshRenderer>()->meshPath = "Assets/first.obj";
    o->AddComponent<MeshRenderer>()->meshPath = "Assets/second.obj";
    const std::string before = SceneSerializer::Serialize(a);

    s1->color = Color(0, 0, 1, 1);
    const std::string after = SceneSerializer::Serialize(a);
    SceneDiff::Result diff = SceneDiff::Compute(before, after);
    CHECK(diff.ok && !diff.Empty());
    std::string applied, err;
    CHECK(SceneDiff::Apply(before, diff, &applied, &err));
    Scene undone("undone");
    CHECK(SceneSerializer::Deserialize(undone, before));
    auto sprites = undone.Find("O")->GetComponents<SpriteRenderer>();
    CHECK(sprites.size() == 2 && sprites[0]->glyph == 'A' && sprites[1]->glyph == 'B' && sprites[1]->color.g == 1.0f);
    Scene redone("redone");
    CHECK(SceneSerializer::Deserialize(redone, applied));
    auto redoneSprites = redone.Find("O")->GetComponents<SpriteRenderer>();
    CHECK(redoneSprites.size() == 2 && redoneSprites[1]->color.b == 1.0f && redoneSprites[0]->color.r == 1.0f);

    // Ours changes the first sprite, theirs the second. Both changes stay.
    s1->color = Color(0, 1, 0, 1);
    const std::string base = SceneSerializer::Serialize(a);
    s0->color = Color(0.5f, 0, 0, 1);
    const std::string ours = SceneSerializer::Serialize(a);
    s0->color = Color(1, 0, 0, 1);
    s1->color = Color(0, 0, 1, 1);
    const std::string theirs = SceneSerializer::Serialize(a);
    SceneMerge::Result merged = SceneMerge::Merge(base, ours, theirs);
    CHECK(merged.ok);
    Scene m("m");
    CHECK(SceneSerializer::Deserialize(m, merged.text, &err));
    auto ms = m.Find("O") ? m.Find("O")->GetComponents<SpriteRenderer>() : std::vector<SpriteRenderer*>{};
    CHECK(ms.size() == 2 && ms[0]->color.r == 0.5f && ms[1]->color.b == 1.0f);

    GameObject* copy = a.Duplicate(*o);
    CHECK(copy != nullptr);
    auto cs = copy ? copy->GetComponents<SpriteRenderer>() : std::vector<SpriteRenderer*>{};
    auto cm = copy ? copy->GetComponents<MeshRenderer>() : std::vector<MeshRenderer*>{};
    CHECK(cs.size() == 2 && cs[0]->glyph == 'A' && cs[1]->glyph == 'B');
    CHECK(cm.size() == 2 && cm[0]->meshPath == "Assets/first.obj" && cm[1]->meshPath == "Assets/second.obj");

    const std::vector<std::string> paths = SceneSerializer::CollectAssetPaths(a);
    auto has = [&](const char* p) { for (const auto& s : paths) if (s == p) return true; return false; };
    CHECK(has("Assets/first.obj") && has("Assets/second.obj"));
}

void TestOldScenes() {
    RUN_SUITE("example scenes still load, and a resave drops nothing");
    namespace fs = std::filesystem;
    const std::string root = OKAY_SOURCE_DIR;
    int files = 0, bad = 0;
    for (const char* dir : {"/examples", "/tests/export"}) {
        std::error_code ec;
        const fs::path base = fs::path(root + dir);
        if (!fs::exists(base, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(base, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec || !it->is_regular_file(ec)) continue;
            const auto ext = it->path().extension().string();
            if (ext != ".okayscene" && ext != ".okayprefab") continue;
            ++files;
            Scene loaded(it->path().filename().string());
            std::string err;
            if (!SceneSerializer::LoadFromFile(loaded, it->path().string(), &err, nullptr)) {
                std::cerr << "    did not load " << it->path().string() << ": " << err << "\n";
                ++bad;
                CHECK(false);
                continue;
            }
            const auto had = TypeCounts(loaded);
            const std::string text1 = SceneSerializer::Serialize(loaded);
            Scene mid("mid");
            if (!SceneSerializer::Deserialize(mid, text1, &err, nullptr)) {
                std::cerr << "    resave of " << it->path().filename().string() << " did not load: " << err << "\n";
                ++bad;
                CHECK(false);
                continue;
            }
            const std::string text2 = SceneSerializer::Serialize(mid);
            Scene again("again");
            CHECK(SceneSerializer::Deserialize(again, text2, &err, nullptr));
            const std::string text3 = SceneSerializer::Serialize(again);
            if (text2 != text3) {
                std::cerr << "    " << it->path().filename().string() << " is not stable\n";
                ShowDiff(text2, text3);
                ++bad;
                CHECK(false);
            }
            const auto got = TypeCounts(mid);
            for (const auto& [type, n] : had)
                if (got.count(type) == 0 || got.at(type) < n) {
                    std::cerr << "    " << it->path().filename().string() << " lost " << type << " (" << n
                              << " -> " << (got.count(type) ? got.at(type) : 0) << ")\n";
                    ++bad;
                    CHECK(false);
                }
        }
    }
    CHECK(files >= 40);
    CHECK(bad == 0);
    std::cout << "  " << files << " example scenes and prefabs\n";
}

}  // namespace

int main() {
    okaytest::Sandbox sandbox("duplicate_components", OKAY_SOURCE_DIR);
    TestEveryType();
    TestOrder();
    TestPrefabApply();
    TestUndoDiffMergeCopyExport();
    TestOldScenes();
    CHECK(sandbox.Verify());
    TEST_MAIN_RESULT();
}
