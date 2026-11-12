// Save coverage: every component type the engine has, as the scene file keeps it. For each type: add one to
// an object, switch it off, save the scene, load it into a new scene, and check the component is still there,
// still switched off, and that saving the loaded scene gives the same text. A type the file silently dropped
// would fail here by name. Also: two components of one type (compound colliders, a music bed and a sound
// effect) both survive, in order, with their own values.
//
// The list below is every concrete class that derives from Component in the engine headers (it was made
// from them; the editor's Add Component menu offers 192 of these). A new component that is not listed here is
// caught by the second check at the end: the property registry's types must all be in the list.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/InventoryPlayer.hpp"
#include "okay/Components/Seat.hpp"
#include "okay/Components/SettingsScreen.hpp"
#include "okay/Components/UIAnimation.hpp"
#include "okay/Components/WorldSpaceUI.hpp"
#include "okay/Testing/PlayTest.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/PropertyRegistry.hpp"

#include <map>
#include <set>
#include <sstream>
#include <string>
#include <typeindex>
#include <vector>

using namespace okay;

#define OKAY_SAVE_COMPONENTS(X) \
    X(ActionList) X(AimIK) X(AimOffset) X(AnimStateMachine) X(Animator) X(AntiCheatConfig) X(AudioSource) \
    X(AutoSave) X(BehaviorTreeRunner) X(BleedingStat) X(BlendShapes) X(BlockBuilder) X(BoxCollider2D) \
    X(BoxCollider3D) X(BuildMode) X(BuildPiece) X(Buildable) X(BuilderMode) X(Camera) X(CameraFollow) X(Canvas) \
    X(CapsuleCollider2D) X(CapsuleCollider3D) X(CarryWeightStat) X(ChainIK) X(Character) X(CharacterController2D) \
    X(CharacterController3D) X(CheckpointTrigger) X(ChestInventory) X(CinemachineBrain) X(CircleCollider2D) \
    X(ClickToMoveController) X(Collectible) X(Consumables) X(Crafting) X(CraftingMenu) X(CraftingStation) \
    X(Crosshair) X(CylinderCollider3D) X(DamageOnTouch) X(DayNightCycle) X(Destructible) X(DialogueRunner) \
    X(DollyCart) X(DollyPath) X(Draggable) X(DropZone) X(EdgeCollider2D) X(Equipment) X(testing::EventProbe) \
    X(EventSystem) X(FirstPersonController) X(FirstPersonHand) X(FirstPersonHands) X(Flashlight) \
    X(FollowTarget2D) X(FootIK) X(FreeRoamController) X(FullBodyIK) X(GridInventory) X(GridInventoryUI) \
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
    X(UIDamageNumbers) X(UIInventoryBind) X(UIDialogueBox) X(UIStyleVariant) X(UIDocument) X(UIDraggable) X(UIDropTarget) X(UIDropdown) X(UIFlexItem) X(UIFrame) X(UIImage) \
    X(UIInputField) X(UIKeybindField) X(UILayoutGroup) X(UIListView) X(UIMask) X(UIModal) X(UIPanel) \
    X(UIProgressBar) X(UIRadialProgress) X(UIRating) X(UIRawImage) X(UISafeArea) X(UIScrollView) X(UISlider) \
    X(UISlotGrid) X(UIStepper) X(UITabs) X(UITextBind) X(UITheme) X(UIToast) X(UIToastItem) X(UIToggle) \
    X(UITooltip) X(VehicleController) X(VehicleController2D) X(VirtualCamera) X(VisualScriptComponent) \
    X(VoxelDigger) X(VoxelTerrain) X(Volume) X(Water) X(WeatherRig) X(WetnessStat) X(WorldSpaceUI) X(WorldStreamer) \
    X(WorldUI)

namespace {

struct Outcome { bool present = false, flagKept = false, stable = false; };

template <class T>
Outcome RoundTrip(const char* name) {
    Outcome o;
    Scene a("a");
    GameObject* g = a.CreateGameObject("Subject");
    T* c = g->AddComponent<T>();
    if (!c) return o;
    c->enabled = false;                                   // a flag every component has: it must survive too
    const std::string text = SceneSerializer::Serialize(a);
    Scene b("b");
    std::string err;
    if (!SceneSerializer::Deserialize(b, text, &err, nullptr)) { std::cerr << "    " << name << ": the saved text did not load: " << err << "\n"; return o; }
    GameObject* g2 = b.Find("Subject");
    T* c2 = g2 ? g2->GetComponent<T>() : nullptr;
    o.present = c2 != nullptr;
    o.flagKept = c2 && !c2->enabled;
    const std::string again = SceneSerializer::Serialize(b);
    o.stable = again == text;
    if (!o.stable && o.present) {   // say which line changed, so a failure can be chased
        std::istringstream x(text), y(again);
        std::string lx, ly;
        while (std::getline(x, lx) && std::getline(y, ly))
            if (lx != ly) { std::cerr << "    " << name << " first differing line:\n      saved:  " << lx.substr(0, 160) << "\n      resaved:" << ly.substr(0, 160) << "\n"; break; }
    }
    return o;
}

}  // namespace

int main() {
    RUN_SUITE("save_coverage");
    Log::console = false;

    std::vector<std::string> dropped, notSwitchable, unstable;
    std::set<std::string> listed;
    int total = 0;
    int kept = 0;
#define X(T)                                                                                  \
    {                                                                                         \
        ++total;                                                                              \
        listed.insert(#T);                                                                    \
        const Outcome o = RoundTrip<T>(#T);                                                   \
        if (!o.present) dropped.push_back(#T);                                                \
        else { ++kept; if (!o.flagKept) notSwitchable.push_back(#T); if (!o.stable) unstable.push_back(#T); } \
    }
    OKAY_SAVE_COMPONENTS(X)
#undef X

    auto show = [](const char* what, const std::vector<std::string>& v) {
        if (v.empty()) return;
        std::cerr << "    " << what << ":";
        for (const auto& n : v) std::cerr << " " << n;
        std::cerr << "\n";
    };
    show("not saved at all", dropped);
    show("saved, but the Enabled switch is lost", notSwitchable);
    show("saved, but a second save gives different text", unstable);
    std::cout << "  " << total << " component types: " << kept << " saved and loaded back, " << dropped.size() << " not saved\n";

    // Types that are never authored in a scene file, so a scene without them is complete:
    //   StatComponent          the base class of the stat components (Health, Hunger, ...), which are saved
    //   TimedDestroy           the timer Destructible adds to its own debris
    //   UIToastItem            one toast row the UIToast component builds while it plays
    //   WeatherRig             the particle rig the scene's weather settings make (those settings are saved)
    //   NetworkAvatarMotion    the motion driver NetworkPlayerSpawner puts on each avatar it spawns
    //   testing::EventProbe    a test helper (okay/Testing/PlayTest.hpp)
    //   SettingsScreen         rebuilt on its own: SettingsScreen::Ensure binds the "Settings" panel when Play starts
    //   SteamManager           added by game code at startup (docs/steam_release.md), not a scene object
    //   CheckpointTrigger      the Save Framework's: added by game code, saved by the framework (docs/save_framework.md)
    //   (Saveable and SaveConfig are saved in the scene file: the no-code save route, docs/save_framework.md)
    const std::set<std::string> kRuntimeOnly = {"StatComponent", "TimedDestroy", "UIToastItem", "WeatherRig", "NetworkAvatarMotion",
                                                "testing::EventProbe", "SettingsScreen", "SteamManager", "CheckpointTrigger"};
    for (const auto& n : dropped) CHECK(kRuntimeOnly.count(n) != 0);
    for (const auto& n : kRuntimeOnly) CHECK(std::find(dropped.begin(), dropped.end(), n) != dropped.end());   // keep this list honest: remove a type once it is saved
    CHECK(notSwitchable.empty());
    // Saved once and loaded, these come back in another form by design: the three controllers are read into the
    // one Player Controller (older files had one record per controller), and a Voxel Terrain / Water builds its mesh
    // renderer when it loads. From the second save on the text no longer changes.
    const std::set<std::string> kChangesOnce = {"FirstPersonController", "ThirdPersonController", "PlayerController", "VoxelTerrain", "Water"};
    for (const auto& n : unstable) CHECK(kChangesOnce.count(n) != 0);
    CHECK(total >= 215);

    // The property registry knows 26 of these types by name: every one must be in the list above.
    RegisterBuiltinProperties();
    int registry = 0;
    for (const auto& info : PropertyRegistry::All()) {
        ++registry;
        if (info->name == "Transform" || info->objectLevel) continue;   // the Transform and the object's own fields are not components of the list
        if (!listed.count(info->name)) std::cerr << "    registry type not in the list: " << info->name << "\n";
        CHECK(listed.count(info->name) != 0);
    }
    CHECK(registry >= 26);

    // ---- two of one type -------------------------------------------------------------------------------
    {
        Scene a("a");
        GameObject* g = a.CreateGameObject("Compound");
        auto* b1 = g->AddComponent<BoxCollider3D>();  b1->size = {1, 2, 3};  b1->offset = {0, 1, 0};
        auto* b2 = g->AddComponent<BoxCollider3D>();  b2->size = {4, 5, 6};  b2->offset = {0, 0, 7};  b2->isTrigger = true;
        auto* s1 = g->AddComponent<SphereCollider3D>(); s1->radius = 2.5f;
        auto* s2 = g->AddComponent<SphereCollider3D>(); s2->radius = 0.75f; s2->enabled = false;
        auto* c1 = g->AddComponent<CapsuleCollider3D>(); c1->radius = 0.4f;
        auto* c2 = g->AddComponent<CapsuleCollider3D>(); c2->radius = 0.9f;
        auto* d1 = g->AddComponent<BoxCollider2D>(); d1->size = {2, 3};
        auto* d2 = g->AddComponent<BoxCollider2D>(); d2->size = {5, 7}; d2->offset = {1, 1};
        auto* e1 = g->AddComponent<CircleCollider2D>(); e1->radius = 3.0f;
        auto* e2 = g->AddComponent<CircleCollider2D>(); e2->radius = 6.0f;
        auto* m1 = g->AddComponent<AudioSource>(); m1->clipPath = "music.ogg"; m1->loop = true; m1->volume = 0.4f;
        auto* m2 = g->AddComponent<AudioSource>(); m2->clipPath = "sfx.wav"; m2->volume = 0.9f; m2->pitch = 1.5f;
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("b");
        std::vector<std::string> warnings;
        CHECK(SceneSerializer::Deserialize(b, text, nullptr, &warnings));
        bool twoWarning = false;
        for (const auto& w : warnings) if (w.find("components; only the first is saved") != std::string::npos) twoWarning = true;
        CHECK(!twoWarning);                                           // these types are saved per instance, so loading is quiet
        GameObject* r = b.Find("Compound");
        CHECK(r != nullptr);
        if (r) {
            auto boxes = r->GetComponents<BoxCollider3D>();
            CHECK(boxes.size() == 2);
            if (boxes.size() == 2) {
                CHECK(boxes[0]->size.x == 1 && boxes[0]->size.y == 2 && boxes[0]->size.z == 3 && !boxes[0]->isTrigger);
                CHECK(boxes[1]->size.x == 4 && boxes[1]->size.z == 6 && boxes[1]->offset.z == 7 && boxes[1]->isTrigger);
            }
            auto spheres = r->GetComponents<SphereCollider3D>();
            CHECK(spheres.size() == 2 && spheres[0]->radius == 2.5f && spheres[1]->radius == 0.75f);
            CHECK(spheres.size() == 2 && spheres[0]->enabled && !spheres[1]->enabled);   // each keeps its own switch
            auto caps = r->GetComponents<CapsuleCollider3D>();
            CHECK(caps.size() == 2 && caps[0]->radius == 0.4f && caps[1]->radius == 0.9f);
            auto b2d = r->GetComponents<BoxCollider2D>();
            CHECK(b2d.size() == 2 && b2d[0]->size.x == 2 && b2d[1]->size.x == 5 && b2d[1]->offset.y == 1);
            auto circ = r->GetComponents<CircleCollider2D>();
            CHECK(circ.size() == 2 && circ[0]->radius == 3.0f && circ[1]->radius == 6.0f);
            auto audio = r->GetComponents<AudioSource>();
            CHECK(audio.size() == 2);
            if (audio.size() == 2) {
                CHECK(audio[0]->clipPath == "music.ogg" && audio[0]->loop && audio[0]->volume == 0.4f);
                CHECK(audio[1]->clipPath == "sfx.wav" && !audio[1]->loop && audio[1]->volume == 0.9f && audio[1]->pitch == 1.5f);
            }
        }
        CHECK(SceneSerializer::Serialize(b) == text);                 // and it is stable
        // One of a type still writes exactly what it always did (no extra record, same text as the old format).
        Scene one("one");
        GameObject* g1 = one.CreateGameObject("One");
        g1->AddComponent<BoxCollider3D>();
        const std::string t1 = SceneSerializer::Serialize(one);
        std::size_t count = 0;
        for (std::size_t at = t1.find("boxcollider3d"); at != std::string::npos; at = t1.find("boxcollider3d", at + 1)) ++count;
        CHECK(count == 1);
    }

    // ---- scene-level settings and the object's own fields -------------------------------------------------------
    {
        using RS = Scene::RenderSettings;
        const RS d;
        Scene a("A scene, \"quoted\" and caf\xC3\xA9");
        RS& r = a.renderSettings;
        int values = 0;
        r.skybox = false; r.skyTop = Color{0.1f, 0.2f, 0.3f, 1}; r.skyHorizon = Color{0.4f, 0.5f, 0.6f, 1}; r.skyBottom = Color{0.7f, 0.8f, 0.9f, 1};
        r.ambient = 0.55f; r.fog = true; r.fogColor = Color{0.9f, 0.8f, 0.7f, 1}; r.fogStart = 5.5f; r.fogEnd = 55.5f;
        r.vignette = 0.45f; r.tonemap = false; r.skyHorizonPos = 0.65f; r.skySun = true; r.skySunX = 0.2f; r.skySunY = 0.4f; r.skySunSize = 0.09f;
        r.skySunColor = Color{1, 0.5f, 0.25f, 1}; r.skyStars = true; r.skyStarDensity = 0.8f; r.skyStarBright = 0.3f;
        r.ambientMode = RS::AmbientSky; r.ambientSkyTint = 1.2f;
        r.fogMode = RS::FogHeight; r.fogDensity = 0.07f; r.fogHeight = 12.5f; r.fogFalloff = 0.33f; r.fogMatchSky = true;
        r.skyMode = RS::SkyProcedural; r.skySunDisc = 4.5f; r.skySunHalo = 0.9f; r.skyHaze = 0.2f; r.cloudCover = 0.8f; r.cloudDensity = 0.3f; r.cloudSpeed = 2.5f; r.cloudScale = 1.75f;
        auto& p = r.post;
        p.exposure = 1.7f; p.tonemap = RS::PostSettings::TonemapFilmic; p.bloom = true; p.bloomThreshold = 0.8f; p.bloomIntensity = 0.9f; p.bloomRadius = 1.6f;
        p.grading = true; p.temperature = 0.3f; p.tint = -0.2f; p.saturation = 1.3f; p.contrast = 1.2f;
        p.lift = {0.01f, 0.02f, 0.03f}; p.gamma = {-0.1f, 0.0f, 0.1f}; p.gain = {0.2f, -0.2f, 0.05f};
        p.vignetteSmooth = 0.8f; p.fxaa = true; p.ssao = true; p.ssaoRadius = 0.9f; p.ssaoIntensity = 1.4f; p.dof = true; p.dofFocus = 6.5f; p.dofRange = 3.5f;
        auto& w = r.weather;
        w.preset = RS::WeatherSettings::Storm; w.intensity = 0.9f; w.fade = 4.5f; w.wind = {1.5f, 0.0f, -2.5f}; w.thunderSound = "Assets/Audio/thunder one.wav";
        a.uiFont = "Assets/Fonts/My Font.ttf";
        a.physics().gravity = {1.5f, -3.25f};
        a.physics3D().gravity = {0.5f, -4.75f, 2.25f};
        a.ApplyLayerCollisionPreset(Scene::LayerCollisionPreset::SameLayerOnly);

        Scene b("b");
        CHECK(SceneSerializer::Deserialize(b, SceneSerializer::Serialize(a)));
        const RS& q = b.renderSettings;
        auto col = [](const Color& x, const Color& y) { return x.r == y.r && x.g == y.g && x.b == y.b; };   // the file keeps RGB
        auto vec = [](const Vec3& x, const Vec3& y) { return x.x == y.x && x.y == y.y && x.z == y.z; };
        int bad = 0;
#define SAME(expr) do { ++values; if (!(expr)) { ++bad; std::cerr << "    not kept: " #expr "\n"; } } while (0)
        SAME(q.skybox == r.skybox && r.skybox != d.skybox);
        SAME(col(q.skyTop, r.skyTop)); SAME(col(q.skyHorizon, r.skyHorizon)); SAME(col(q.skyBottom, r.skyBottom));
        SAME(q.ambient == r.ambient); SAME(q.fog == r.fog); SAME(col(q.fogColor, r.fogColor)); SAME(q.fogStart == r.fogStart); SAME(q.fogEnd == r.fogEnd);
        SAME(q.vignette == r.vignette); SAME(q.tonemap == r.tonemap); SAME(q.skyHorizonPos == r.skyHorizonPos); SAME(q.skySun == r.skySun);
        SAME(q.skySunX == r.skySunX); SAME(q.skySunY == r.skySunY); SAME(q.skySunSize == r.skySunSize); SAME(col(q.skySunColor, r.skySunColor));
        SAME(q.skyStars == r.skyStars); SAME(q.skyStarDensity == r.skyStarDensity); SAME(q.skyStarBright == r.skyStarBright);
        SAME(q.ambientMode == r.ambientMode); SAME(q.ambientSkyTint == r.ambientSkyTint);
        SAME(q.fogMode == r.fogMode); SAME(q.fogDensity == r.fogDensity); SAME(q.fogHeight == r.fogHeight); SAME(q.fogFalloff == r.fogFalloff); SAME(q.fogMatchSky == r.fogMatchSky);
        SAME(q.skyMode == r.skyMode); SAME(q.skySunDisc == r.skySunDisc); SAME(q.skySunHalo == r.skySunHalo); SAME(q.skyHaze == r.skyHaze);
        SAME(q.cloudCover == r.cloudCover); SAME(q.cloudDensity == r.cloudDensity); SAME(q.cloudSpeed == r.cloudSpeed); SAME(q.cloudScale == r.cloudScale);
        const auto& pq = q.post;
        SAME(pq.exposure == p.exposure); SAME(pq.tonemap == p.tonemap); SAME(pq.bloom == p.bloom); SAME(pq.bloomThreshold == p.bloomThreshold);
        SAME(pq.bloomIntensity == p.bloomIntensity); SAME(pq.bloomRadius == p.bloomRadius); SAME(pq.grading == p.grading); SAME(pq.temperature == p.temperature);
        SAME(pq.tint == p.tint); SAME(pq.saturation == p.saturation); SAME(pq.contrast == p.contrast);
        SAME(vec(pq.lift, p.lift)); SAME(vec(pq.gamma, p.gamma)); SAME(vec(pq.gain, p.gain));
        SAME(pq.vignetteSmooth == p.vignetteSmooth); SAME(pq.fxaa == p.fxaa); SAME(pq.ssao == p.ssao); SAME(pq.ssaoRadius == p.ssaoRadius);
        SAME(pq.ssaoIntensity == p.ssaoIntensity); SAME(pq.dof == p.dof); SAME(pq.dofFocus == p.dofFocus); SAME(pq.dofRange == p.dofRange);
        const auto& wq = q.weather;
        SAME(wq.preset == w.preset); SAME(wq.intensity == w.intensity); SAME(wq.fade == w.fade); SAME(vec(wq.wind, w.wind)); SAME(wq.thunderSound == w.thunderSound);
        SAME(b.Name() == a.Name()); SAME(b.uiFont == a.uiFont);
        SAME(b.physics().gravity.x == 1.5f && b.physics().gravity.y == -3.25f);
        SAME(vec(b.physics3D().gravity, a.physics3D().gravity));
        SAME(b.LayersCollide(3, 3) && !b.LayersCollide(3, 4) && b.HasCustomLayerCollisions());
#undef SAME
        CHECK(bad == 0);
        std::cout << "  scene settings: " << values << " values (render, post-processing, weather, physics gravity, layer matrix, UI font, name) kept\n";
        CHECK(values >= 62);

        // The object's own authored fields: every one set to something other than its default.
        Scene s("objects");
        GameObject* parent = s.CreateGameObject("Parent \"one\"");
        GameObject* c1 = s.CreateGameObject("Child A");
        GameObject* c2 = s.CreateGameObject("Child B");
        c1->transform->SetParent(parent->transform, false);
        c2->transform->SetParent(parent->transform, false);
        parent->transform->localPosition = {1.5f, -2.5f, 3.5f};
        parent->transform->localRotation = Quat::Euler(10, 20, 30);
        parent->transform->localScale = {2, 3, 4};
        parent->active = false;
        parent->tag = "Boss";
        parent->layer = 7;
        parent->isStatic = true;
        parent->editorLocked = true;
        parent->uiDrawOrder = 5;
        parent->sourceScene = "Assets/Other Scene.okayscene";
        parent->prefabSource = "Assets/Prefabs/Thing.okayprefab";
        const std::string guid = parent->Guid();
        c2->transform->SetSiblingIndex(0);   // reordered siblings
        Scene t("t");
        CHECK(SceneSerializer::Deserialize(t, SceneSerializer::Serialize(s)));
        GameObject* pb = t.FindByGuid(guid);
        CHECK(pb != nullptr);
        if (pb) {
            CHECK(pb->name == "Parent \"one\"" && !pb->active && pb->tag == "Boss" && pb->layer == 7 && pb->isStatic && pb->editorLocked);
            CHECK(pb->uiDrawOrder == 5 && pb->sourceScene == "Assets/Other Scene.okayscene" && pb->prefabSource == "Assets/Prefabs/Thing.okayprefab");
            CHECK(vec(pb->transform->localPosition, Vec3{1.5f, -2.5f, 3.5f}) && vec(pb->transform->localScale, Vec3{2, 3, 4}));
            CHECK(std::fabs(Quat::Dot(pb->transform->localRotation, Quat::Euler(10, 20, 30))) > 0.99999f);
            auto kids = pb->transform->Children();
            CHECK(kids.size() == 2 && kids[0]->gameObject->name == "Child B" && kids[1]->gameObject->name == "Child A");   // sibling order
        }
        std::cout << "  object fields: name, guid, active, tag, layer, static, locked, UI order, source scene, prefab link, transform, parent, sibling order kept\n";
    }

    TEST_MAIN_RESULT();
}
