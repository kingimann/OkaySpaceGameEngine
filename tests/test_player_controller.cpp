#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <string>

using namespace okay;
using Mode = PlayerController::ViewMode;

// The one Player Controller: its View setting (First Person only, Third Person
// only, Both), the toggle key, what a disabled view leaves behind, and scenes
// saved with the separate First Person / Third Person / Player View components.

static void Ground(Scene& sc) {
    sc.CreateGameObject("Directional Light")->AddComponent<Light>();
    GameObject* ground = sc.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -0.5f, 0};
    ground->transform->localScale = {40, 1, 40};
    ground->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
    ground->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    ground->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
}

static GameObject* Body(Scene& sc) {
    GameObject* player = sc.CreateGameObject("Player");
    player->transform->localPosition = {0, 1.0f, 0};
    player->AddComponent<Character>()->Apply();
    HumanoidLibrary::EnsureAnimator(player);
    player->AddComponent<Rigidbody3D>();
    auto* col = player->AddComponent<BoxCollider3D>();
    col->size = {0.6f, 1.8f, 0.6f};
    col->offset = {0.0f, 0.9f, 0.0f};
    return player;
}

static Camera* EyeCamera(Scene& sc, GameObject* player) {
    return Templates::MountEyeCamera(sc, player, "Main Camera")->GetComponent<Camera>();
}

// A player with the Player Controller in `mode`, on a flat world.
static GameObject* NewPlayer(Scene& sc, Mode mode, PlayerView::View start = PlayerView::View::Third) {
    Ground(sc);
    GameObject* p = Body(sc);
    PlayerController::Add(p, mode, start);
    EyeCamera(sc, p);
    return p;
}

static Camera* ChildCamera(GameObject* p) {
    for (Transform* c : p->transform->Children())
        if (c && c->gameObject)
            if (auto* cam = c->gameObject->GetComponent<Camera>()) return cam;
    return nullptr;
}

static void Frames(Scene& sc, int n, const std::vector<char>& keys = {}) {
    Input::FeedKeys(keys);
    for (int i = 0; i < n; ++i) sc.Update(1.0f / 60.0f);
    Input::FeedKeys({});
}

static void PressV(Scene& sc) {
    Input::FeedKeys({'v'});
    sc.Update(1.0f / 60.0f);
    Input::FeedKeys({});
}

static bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

static int ControllerComponents(GameObject* go) {
    int n = 0;
    for (const auto& c : go->Components())
        if (dynamic_cast<PlayerController*>(c.get()) || dynamic_cast<FirstPersonController*>(c.get()) ||
            dynamic_cast<ThirdPersonController*>(c.get()) || dynamic_cast<PlayerView*>(c.get())) ++n;
    return n;
}

// What a scripted run leaves behind.
struct Pose { Vec3 pos, cam; float camPitch = 0; bool fpOwns = false; };

static Pose Run(Scene& sc) {
    GameObject* p = sc.Find("Player");
    Camera* cam = ChildCamera(p);
    sc.Start();
    Frames(sc, 30);
    Frames(sc, 50, {'w'});
    Frames(sc, 20, {'a'});
    PressV(sc);
    Frames(sc, 40);
    Frames(sc, 40, {'w'});
    Frames(sc, 10, {' '});
    Frames(sc, 40);
    Pose r;
    r.pos = p->transform->Position();
    r.cam = cam->transform->Position();
    r.camPitch = cam->transform->Rotation().ToEuler().x;
    r.fpOwns = FirstPersonOwns(p);
    return r;
}

// A scene as one made before the Player Controller wrote it: bare controllers.
// fp / tp say which are on; pv is 0 none, 1 toggle on, 2 toggle off (starts third).
static void LegacyPlayer(Scene& sc, bool fp, bool tp, int pv) {
    Ground(sc);
    GameObject* p = Body(sc);
    if (fp) p->AddComponent<FirstPersonController>();
    if (tp) p->AddComponent<ThirdPersonController>();
    if (pv) {
        auto* v = Templates::AddViewToggle(p, PlayerView::View::Third, 0.45f);
        v->allowToggle = pv == 1;
    }
    EyeCamera(sc, p);
}

static void CheckSame(const Pose& a, const Pose& b) {
    CHECK_NEAR(a.pos.x, b.pos.x, 1e-3f); CHECK_NEAR(a.pos.y, b.pos.y, 1e-3f); CHECK_NEAR(a.pos.z, b.pos.z, 1e-3f);
    CHECK_NEAR(a.cam.x, b.cam.x, 1e-3f); CHECK_NEAR(a.cam.y, b.cam.y, 1e-3f); CHECK_NEAR(a.cam.z, b.cam.z, 1e-3f);
    CHECK_NEAR(a.camPitch, b.camPitch, 1e-2f);
    CHECK(a.fpOwns == b.fpOwns);
}

// Save `old`, load it back, run both the same way, and hand back what the loaded one holds.
static PlayerController* MigrateAndCompare(bool fp, bool tp, int pv, bool compare = true) {
    static Scene oldScene("old"), loaded("loaded");
    oldScene.Clear(); loaded.Clear();
    oldScene.physicsEnabled = true; loaded.physicsEnabled = true;
    LegacyPlayer(oldScene, fp, tp, pv);
    const std::string text = SceneSerializer::Serialize(oldScene);
    CHECK(Has(text, "fpctrl") == fp);              // the legacy file has the separate records
    CHECK(Has(text, "tpctrl") == tp);
    CHECK(SceneSerializer::Deserialize(loaded, text));
    GameObject* p = loaded.Find("Player");
    CHECK(p != nullptr);
    if (!p) return nullptr;
    CHECK(ControllerComponents(p) >= 1);
    if (compare) CheckSame(Run(oldScene), Run(loaded));
    return PlayerController::Of(p);
}

int main() {
    RUN_SUITE("player controller");

    // --- One component: adding it is the whole setup ---
    {
        Scene sc("one");
        GameObject* p = sc.CreateGameObject("P");
        auto* pc = PlayerController::Add(p);
        CHECK(pc && pc->viewMode() == Mode::Both);
        CHECK(pc->first() && pc->third() && pc->toggle());
        CHECK(pc->toggle()->allowToggle);
        CHECK(pc->toggle()->toggleKey == 'v');
        CHECK(pc->toggle()->defaultView == PlayerView::View::Third);
        // Removing it takes its parts with it.
        CHECK(p->RemoveComponent(pc));
        CHECK(!p->GetComponent<FirstPersonController>() && !p->GetComponent<ThirdPersonController>() &&
              !p->GetComponent<PlayerView>());
    }

    // --- Changing the View adds and drops parts; a dropped view's settings go with it ---
    {
        Scene sc("modes");
        GameObject* p = sc.CreateGameObject("P");
        auto* pc = PlayerController::Add(p, Mode::Both);
        pc->first()->walkSpeed = 3.3f;
        pc->third()->distance = 9.0f;
        pc->SetViewMode(Mode::First);
        CHECK(pc->first() && !pc->third() && !pc->toggle());
        CHECK_NEAR(pc->first()->walkSpeed, 3.3f, 1e-6f);   // the view that stays keeps its settings
        pc->SetViewMode(Mode::Both);
        CHECK(pc->first() && pc->third() && pc->toggle());
        CHECK(pc->toggle()->defaultView == PlayerView::View::First);   // First -> Both starts at the eyes
        CHECK_NEAR(pc->third()->distance, ThirdPersonController().distance, 1e-6f);   // the dropped view came back fresh
        pc->SetViewMode(Mode::Third);
        CHECK(!pc->first() && pc->third() && !pc->toggle());
        CHECK(ControllerComponents(p) == 2);   // the Player Controller and the orbit
    }

    // --- View = Both: V switches, the eye controller drives first person ---
    {
        Scene sc("both"); sc.physicsEnabled = true;
        GameObject* p = NewPlayer(sc, Mode::Both, PlayerView::View::Third);
        Camera* cam = ChildCamera(p);
        sc.Start();
        Frames(sc, 40);
        CHECK(!FirstPersonOwns(p));
        PressV(sc);
        Frames(sc, 40);
        CHECK(FirstPersonOwns(p));
        Vec3 local = p->transform->InverseTransformPoint(cam->transform->Position());
        CHECK_NEAR(local.y, 1.6f, 0.1f);
        PressV(sc);
        Frames(sc, 40);
        CHECK(!FirstPersonOwns(p));
        local = p->transform->InverseTransformPoint(cam->transform->Position());
        CHECK(local.z > 3.0f);
    }

    // --- View = Both with the toggle off: the start view stays; the key does nothing ---
    {
        Scene sc("both-off"); sc.physicsEnabled = true;
        GameObject* p = NewPlayer(sc, Mode::Both, PlayerView::View::First);
        PlayerController::Of(p)->toggle()->allowToggle = false;
        sc.Start();
        Frames(sc, 30);
        CHECK(FirstPersonOwns(p));
        PressV(sc);
        Frames(sc, 40);
        CHECK(FirstPersonOwns(p));
    }

    // --- View = First Person only: V does nothing, the third-person settings do not exist ---
    {
        Scene sc("first"); sc.physicsEnabled = true;
        GameObject* p = NewPlayer(sc, Mode::First);
        auto* pc = PlayerController::Of(p);
        CHECK(pc->first() && !pc->third() && !pc->toggle());
        Camera* cam = ChildCamera(p);
        sc.Start();
        Frames(sc, 40);
        const Vec3 before = p->transform->InverseTransformPoint(cam->transform->Position());
        PressV(sc);
        Frames(sc, 40);
        const Vec3 after = p->transform->InverseTransformPoint(cam->transform->Position());
        CHECK_NEAR(after.x, before.x, 1e-3f); CHECK_NEAR(after.y, before.y, 0.05f); CHECK_NEAR(after.z, before.z, 1e-3f);
        CHECK_NEAR(after.y, 1.6f, 0.1f);
        CHECK(cam->ignoreObject == p);
        CHECK(PlayerView::FindForScript(p, true) == nullptr);   // a script cannot turn a toggle on behind the setting
        CHECK(pc->CurrentView() == 0);
    }

    // --- View = Third Person only ---
    {
        Scene sc("third"); sc.physicsEnabled = true;
        GameObject* p = NewPlayer(sc, Mode::Third);
        auto* pc = PlayerController::Of(p);
        CHECK(!pc->first() && pc->third() && !pc->toggle());
        Camera* cam = ChildCamera(p);
        sc.Start();
        Frames(sc, 40);
        const Vec3 before = p->transform->InverseTransformPoint(cam->transform->Position());
        PressV(sc);
        Frames(sc, 40);
        const Vec3 after = p->transform->InverseTransformPoint(cam->transform->Position());
        CHECK_NEAR(after.x, before.x, 1e-3f); CHECK_NEAR(after.y, before.y, 0.05f); CHECK_NEAR(after.z, before.z, 1e-3f);
        CHECK(after.z > 3.0f);
        CHECK(cam->ignoreObject == nullptr);
        CHECK(!FirstPersonOwns(p));
        CHECK(pc->CurrentView() == 1);
    }

    // --- The movement is the same in every View ---
    {
        Pose poses[3];
        for (int m = 0; m < 3; ++m) {
            Scene sc("move"); sc.physicsEnabled = true;
            GameObject* p = NewPlayer(sc, (Mode)m, PlayerView::View::First);
            sc.Start();
            Frames(sc, 30);
            Frames(sc, 60, {'w'});
            poses[m].pos = p->transform->Position();
        }
        // First person walks along its facing: the same here for First and Both (starting first).
        CHECK_NEAR(poses[0].pos.z, poses[2].pos.z, 1e-3f);
        CHECK(std::fabs(poses[0].pos.z) > 2.0f || std::fabs(poses[0].pos.x) > 2.0f);
        CHECK(std::fabs(poses[1].pos.z) > 1.0f || std::fabs(poses[1].pos.x) > 1.0f);
    }

    // --- A new scene writes only the Player Controller, and it round-trips ---
    {
        Scene sc("save"); sc.physicsEnabled = true;
        GameObject* p = NewPlayer(sc, Mode::Both, PlayerView::View::First);
        auto* pc = PlayerController::Of(p);
        pc->first()->walkSpeed = 3.3f;
        pc->first()->headBob = false;
        pc->third()->distance = 7.5f;
        pc->third()->faceMode = ThirdPersonController::FaceMode::Camera;
        pc->toggle()->toggleKey = 'g';
        pc->toggle()->blendTime = 0.5f;
        pc->toggle()->shoulderOffset = 0.3f;
        const std::string text = SceneSerializer::Serialize(sc);
        CHECK(text.compare(0, 12, "okayscene 3\n") == 0);
        CHECK(Has(text, "playercontroller"));
        CHECK(!Has(text, "fpctrl") && !Has(text, "tpctrl") && !Has(text, "playerview") &&
              !Has(text, "fpfeel") && !Has(text, "tpfeel") && !Has(text, "fpstance") && !Has(text, "tpstance"));
        Scene back("back");
        CHECK(SceneSerializer::Deserialize(back, text));
        GameObject* q = back.Find("Player");
        auto* qc = PlayerController::Of(q);
        CHECK(qc != nullptr);
        if (qc) {
            CHECK(ControllerComponents(q) == 4);   // the Player Controller and its three parts, once each
            CHECK(qc->viewMode() == Mode::Both);
            CHECK(qc->first() && qc->third() && qc->toggle());
            CHECK_NEAR(qc->first()->walkSpeed, 3.3f, 1e-4f);
            CHECK(!qc->first()->headBob);
            CHECK_NEAR(qc->third()->distance, 7.5f, 1e-4f);
            CHECK(qc->third()->faceMode == ThirdPersonController::FaceMode::Camera);
            CHECK(qc->toggle()->toggleKey == 'g');
            CHECK_NEAR(qc->toggle()->blendTime, 0.5f, 1e-4f);
            CHECK_NEAR(qc->toggle()->shoulderOffset, 0.3f, 1e-4f);
            CHECK(qc->toggle()->defaultView == PlayerView::View::First);
            CHECK(qc->toggle()->allowToggle);
        }
        // Saved again, nothing changes.
        CHECK(SceneSerializer::Serialize(back) == text);
    }
    for (int m = 0; m < 2; ++m) {   // First / Third only: just that view's section
        Scene sc("save1"); sc.physicsEnabled = true;
        NewPlayer(sc, (Mode)m);
        const std::string text = SceneSerializer::Serialize(sc);
        CHECK(Has(text, "playercontroller"));
        CHECK(Has(text, "pcfirst") == (m == 0));
        CHECK(Has(text, "pcthird") == (m == 1));
        CHECK(!Has(text, "fpctrl") && !Has(text, "tpctrl") && !Has(text, "playerview"));
        CHECK(!Has(text, " allow="));   // no toggle without a second view
        Scene back("back1");
        CHECK(SceneSerializer::Deserialize(back, text));
        auto* qc = PlayerController::Of(back.Find("Player"));
        CHECK(qc && qc->viewMode() == (Mode)m && (qc->first() != nullptr) == (m == 0) &&
              (qc->third() != nullptr) == (m == 1) && !qc->toggle());
    }

    // --- Old scenes: each layout loads into one Player Controller and behaves as before ---
    {   // First Person only
        auto* pc = MigrateAndCompare(true, false, 0);
        CHECK(pc && pc->viewMode() == Mode::First && pc->first() && !pc->third() && !pc->toggle());
    }
    {   // Third Person only
        auto* pc = MigrateAndCompare(false, true, 0);
        CHECK(pc && pc->viewMode() == Mode::Third && !pc->first() && pc->third() && !pc->toggle());
    }
    {   // Both controllers, no Player View: first person moved the body
        auto* pc = MigrateAndCompare(true, true, 0);
        CHECK(pc && pc->viewMode() == Mode::Both && pc->toggle());
        if (pc && pc->toggle()) {
            CHECK(!pc->toggle()->allowToggle);
            CHECK(pc->toggle()->defaultView == PlayerView::View::First);
        }
    }
    {   // Both controllers + Player View, toggle on
        auto* pc = MigrateAndCompare(true, true, 1);
        CHECK(pc && pc->viewMode() == Mode::Both && pc->toggle());
        if (pc && pc->toggle()) {
            CHECK(pc->toggle()->allowToggle);
            CHECK(pc->toggle()->defaultView == PlayerView::View::Third);
            CHECK_NEAR(pc->toggle()->shoulderOffset, 0.45f, 1e-4f);
        }
    }
    {   // Both controllers + Player View, toggle off: a fixed start view
        auto* pc = MigrateAndCompare(true, true, 2);
        CHECK(pc && pc->viewMode() == Mode::Both && pc->toggle());
        if (pc && pc->toggle()) {
            CHECK(!pc->toggle()->allowToggle);
            CHECK(pc->toggle()->defaultView == PlayerView::View::Third);
        }
    }
    {   // One controller + Player View with the toggle off: the Player View goes
        auto* pc = MigrateAndCompare(false, true, 2, false);
        CHECK(pc && pc->viewMode() == Mode::Third && !pc->toggle());
    }
    {   // One controller + a toggle that is on: a switchable player, so both views
        auto* pc = MigrateAndCompare(true, false, 1, false);
        CHECK(pc && pc->viewMode() == Mode::Both && pc->first() && pc->third() && pc->toggle() && pc->toggle()->allowToggle);
    }
    {   // Settings of the old controllers carry over exactly
        Scene old("old");
        LegacyPlayer(old, true, true, 1);
        GameObject* op = old.Find("Player");
        op->GetComponent<FirstPersonController>()->walkSpeed = 2.7f;
        op->GetComponent<FirstPersonController>()->crouchSpeed = 1.1f;
        op->GetComponent<ThirdPersonController>()->distance = 8.25f;
        op->GetComponent<ThirdPersonController>()->shoulderOffset = 0.6f;
        op->GetComponent<PlayerView>()->toggleKey = 'c';
        Scene sc("new");
        CHECK(SceneSerializer::Deserialize(sc, SceneSerializer::Serialize(old)));
        auto* pc = PlayerController::Of(sc.Find("Player"));
        CHECK(pc && pc->first() && pc->third() && pc->toggle());
        if (pc && pc->first() && pc->third() && pc->toggle()) {
            CHECK_NEAR(pc->first()->walkSpeed, 2.7f, 1e-4f);
            CHECK_NEAR(pc->first()->crouchSpeed, 1.1f, 1e-4f);
            CHECK_NEAR(pc->third()->distance, 8.25f, 1e-4f);
            CHECK_NEAR(pc->third()->shoulderOffset, 0.6f, 1e-4f);
            CHECK(pc->toggle()->toggleKey == 'c');
        }
    }

    // --- A prefab: old and new ---
    {
        Scene old("old");
        LegacyPlayer(old, true, true, 1);
        const std::string prefab = SceneSerializer::SerializeObject(*old.Find("Player"));
        CHECK(Has(prefab, "fpctrl"));
        Scene sc("prefab");
        GameObject* g = SceneSerializer::InstantiateFromText(sc, prefab);
        auto* pc = PlayerController::Of(g);
        CHECK(pc && pc->viewMode() == Mode::Both && pc->toggle());
        CHECK(g && ControllerComponents(g) == 4);
        // Saved from the Player Controller it carries the new record only.
        const std::string again = SceneSerializer::SerializeObject(*g);
        CHECK(Has(again, "playercontroller") && !Has(again, "fpctrl") && !Has(again, "tpctrl") && !Has(again, "playerview"));
        Scene sc2("prefab2");
        GameObject* h = SceneSerializer::InstantiateFromText(sc2, again);
        CHECK(h && PlayerController::Of(h) && ControllerComponents(h) == 4);
    }

    // --- The starters carry one Player Controller ---
    {
        Scene sc("starters");
        Templates::ThirdPerson(sc);
        GameObject* p = sc.Find("Player");
        auto* pc = PlayerController::Of(p);
        CHECK(pc && pc->viewMode() == Mode::Both && ControllerComponents(p) == 4);
        CHECK(pc && pc->toggle() && pc->toggle()->defaultView == PlayerView::View::Third);
        Scene fs("starters-fp");
        Templates::FPS(fs);
        auto* fc = PlayerController::Of(fs.Find("Player"));
        CHECK(fc && fc->viewMode() == Mode::Both && fc->toggle() && fc->toggle()->defaultView == PlayerView::View::First);
    }

    // --- Scripts: set_view / get_view go through the Player Controller ---
    {
        Scene sc("script"); sc.physicsEnabled = true;
        GameObject* p = NewPlayer(sc, Mode::Both, PlayerView::View::Third);
        sc.Start();
        Frames(sc, 30);
        PlayerView* v = PlayerView::FindForScript(p, true);
        CHECK(v == PlayerController::Of(p)->toggle());
        if (v) { v->SetView(0); }
        Frames(sc, 40);
        CHECK(FirstPersonOwns(p));
        CHECK(PlayerController::Of(p)->CurrentView() == 0);
    }

    TEST_MAIN_RESULT();
}
