#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <string>

using namespace okay;

// Scenes saved by the 2.839.0 starters put one controller on the player and let
// the view toggle fake the other view from it. Loading such a file (format
// version 2) completes the player: both controllers, the camera on the player.
// The switch then runs as it does on the current starters.

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

// The 2.839.0 First Person starter: eye controller, eye camera, V faked an orbit.
static void OldFirstPerson(Scene& sc) {
    Ground(sc);
    GameObject* player = Body(sc);
    player->AddComponent<FirstPersonController>();
    Templates::AddViewToggle(player, PlayerView::View::First, 0.45f);
    GameObject* camObj = sc.CreateGameObject("FPS Camera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    cam->main = true;
    camObj->transform->SetParent(player->transform, false);
    camObj->transform->localPosition = {0, 1.62f, 0.0f};
}

// The 2.839.0 Third Person starter: orbit controller, a loose main camera, V faked the eyes.
static void OldThirdPerson(Scene& sc) {
    Ground(sc);
    GameObject* player = Body(sc);
    player->AddComponent<ThirdPersonController>();
    Templates::AddViewToggle(player, PlayerView::View::Third, 0.45f);
    GameObject* camObj = sc.CreateGameObject("Main Camera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    cam->main = true;
    camObj->transform->localPosition = {0, 3, 6};
}

static std::string SavedAsVersion(const Scene& sc, int version) {
    std::string text = SceneSerializer::Serialize(sc);
    const std::string head = "okayscene 3\n";
    CHECK(text.compare(0, head.size(), head) == 0);
    return "okayscene " + std::to_string(version) + "\n" + text.substr(head.size());
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

int main() {
    RUN_SUITE("view upgrade");

    // --- An old First Person file: the orbit is the real one, and the body turns with the stick ---
    {
        Scene saved("old fp"); OldFirstPerson(saved);
        Scene sc("fp"); sc.physicsEnabled = true;
        CHECK(SceneSerializer::Deserialize(sc, SavedAsVersion(saved, 2)));
        GameObject* p = sc.Find("Player");
        CHECK(p && p->GetComponent<FirstPersonController>() && p->GetComponent<ThirdPersonController>());
        Camera* cam = p ? ChildCamera(p) : nullptr;
        CHECK(cam && cam->gameObject->name == "FPS Camera");   // the eye camera stays where it was
        if (!p || !cam) return 1;
        CHECK_NEAR(cam->transform->localPosition.y, 1.62f, 1e-3f);

        sc.Start();
        Frames(sc, 30);
        CHECK(FirstPersonOwns(p));
        CHECK(cam->ignoreObject == p);
        PressV(sc);
        Frames(sc, 40);
        Vec3 local = p->transform->InverseTransformPoint(cam->transform->Position());
        CHECK(!FirstPersonOwns(p));                       // the orbit controller drives third person
        CHECK(local.y > 2.6f && local.y < 3.4f);          // the starter's orbit: up and behind, not a level eye-height camera
        CHECK(local.z > 4.2f && local.z < 5.2f);
        CHECK_NEAR(cam->transform->Rotation().ToEuler().x, -18.0f, 3.0f);   // looking down the rest pitch
        CHECK(cam->ignoreObject == nullptr);              // the head is drawn
        CHECK(cam->nearClip > 0.06f);
        Character* ch = p->GetComponent<Character>();
        CHECK(ch && ch->PartsBuilt() && ch->Part(HumanoidMotion::B_HEAD) && ch->Part(HumanoidMotion::B_HEAD)->active);

        const float yaw0 = p->transform->Rotation().ToEuler().y;
        Frames(sc, 40, {'a'});
        const float yaw1 = p->transform->Rotation().ToEuler().y;
        CHECK(std::fabs(yaw1 - yaw0) > 45.0f);            // the body turns toward the step, as third person does

        PressV(sc);
        Frames(sc, 40);
        local = p->transform->InverseTransformPoint(cam->transform->Position());
        CHECK(FirstPersonOwns(p));
        CHECK_NEAR(local.y, 1.6f, 0.1f);
        CHECK(local.z < -0.1f && local.z > -0.4f);
        CHECK(cam->ignoreObject == p);
    }

    // --- An old Third Person file: the camera is mounted on the player, first person is the eye controller ---
    {
        Scene saved("old tp"); OldThirdPerson(saved);
        Scene sc("tp"); sc.physicsEnabled = true;
        CHECK(SceneSerializer::Deserialize(sc, SavedAsVersion(saved, 2)));
        GameObject* p = sc.Find("Player");
        CHECK(p && p->GetComponent<FirstPersonController>() && p->GetComponent<ThirdPersonController>());
        Camera* cam = p ? ChildCamera(p) : nullptr;
        CHECK(cam && cam->gameObject->name == "Main Camera");
        if (!p || !cam) return 1;
        // Mounted at the resting orbit the starter poses: over the shoulder, up and behind.
        CHECK_NEAR(cam->transform->localPosition.x, 0.45f, 1e-3f);
        CHECK(cam->transform->localPosition.y > 2.6f && cam->transform->localPosition.y < 3.4f);
        CHECK(cam->transform->localPosition.z > 4.2f && cam->transform->localPosition.z < 5.2f);

        sc.Start();
        Frames(sc, 30);
        CHECK(!FirstPersonOwns(p));
        Vec3 local = p->transform->InverseTransformPoint(cam->transform->Position());
        CHECK(local.z > 4.0f);
        PressV(sc);
        Frames(sc, 40);
        local = p->transform->InverseTransformPoint(cam->transform->Position());
        CHECK(FirstPersonOwns(p));                        // the eye controller, not an eye faked from the orbit
        CHECK_NEAR(local.y, 1.6f, 0.1f);
        CHECK(local.z < -0.1f && local.z > -0.4f);
        CHECK_NEAR(cam->transform->Rotation().ToEuler().x, 0.0f, 3.0f);   // level, not the orbit's tilt
        CHECK(cam->ignoreObject == p);
        CHECK(cam->nearClip < 0.06f);
        PressV(sc);
        Frames(sc, 40);
        local = p->transform->InverseTransformPoint(cam->transform->Position());
        CHECK(!FirstPersonOwns(p));
        CHECK(local.y > 2.6f && local.z > 4.2f);
        CHECK(cam->ignoreObject == nullptr);
    }

    // --- A file saved by this version with one controller and the toggle on is a switchable
    //     player all the same: the Player Controller gives it both views, the camera stays put ---
    {
        Scene saved("kept"); OldFirstPerson(saved);
        Scene sc("kept2");
        CHECK(SceneSerializer::Deserialize(sc, SceneSerializer::Serialize(saved)));
        GameObject* p = sc.Find("Player");
        CHECK(p && p->GetComponent<FirstPersonController>() && p->GetComponent<ThirdPersonController>());
        CHECK(p && PlayerController::Of(p) && PlayerController::Of(p)->viewMode() == PlayerController::ViewMode::Both);
        CHECK(p && ChildCamera(p) && ChildCamera(p)->gameObject->name == "FPS Camera");
    }

    // --- A toggle that is off is not a switchable player: left alone ---
    {
        Scene saved("off"); OldThirdPerson(saved);
        saved.Find("Player")->GetComponent<PlayerView>()->allowToggle = false;
        Scene sc("off2");
        CHECK(SceneSerializer::Deserialize(sc, SavedAsVersion(saved, 2)));
        GameObject* p = sc.Find("Player");
        CHECK(p && !p->GetComponent<FirstPersonController>() && p->GetComponent<ThirdPersonController>());
        GameObject* cam = sc.Find("Main Camera");
        CHECK(cam && cam->transform->Parent() == nullptr);
    }

    // --- Players that already have both, or a camera of their own, keep it ---
    {
        Scene saved("both"); Templates::ThirdPerson(saved);
        Scene sc("both2");
        CHECK(SceneSerializer::Deserialize(sc, SavedAsVersion(saved, 2)));
        GameObject* p = sc.Find("Player");
        CHECK(p && p->GetComponent<FirstPersonController>() && p->GetComponent<ThirdPersonController>());
        int cams = 0;
        for (Transform* c : p->transform->Children())
            if (c && c->gameObject && c->gameObject->GetComponent<Camera>()) ++cams;
        CHECK(cams == 1);
    }

    // --- Two old players and one loose camera: the second player does not take it off the first ---
    {
        Scene saved("two"); OldThirdPerson(saved);
        GameObject* second = Body(saved);
        second->name = "Player 2";
        second->transform->localPosition = {4, 1, 0};
        second->AddComponent<ThirdPersonController>();
        Templates::AddViewToggle(second, PlayerView::View::Third, 0.45f);
        Scene sc("two2");
        CHECK(SceneSerializer::Deserialize(sc, SavedAsVersion(saved, 2)));
        GameObject* p1 = sc.Find("Player");
        GameObject* p2 = sc.Find("Player 2");
        GameObject* cam = sc.Find("Main Camera");
        CHECK(p1 && p2 && cam);
        if (p1 && p2 && cam) {
            CHECK(cam->transform->Parent() == p1->transform);
            CHECK(p2->GetComponent<FirstPersonController>() && p2->GetComponent<ThirdPersonController>());
        }
    }

    TEST_MAIN_RESULT();
}
