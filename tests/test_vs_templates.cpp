// Every visual-script template loads, and each one changes a real scene.
#include "test_framework.hpp"
#include "VisualScriptTemplates.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include <Okay.hpp>
#include <cmath>
#include <filesystem>
#include <string>

using namespace okay;
using namespace okay::editor::vsed;

namespace {

VisualScriptComponent* Attach(Scene& scene, const char* name, const VsTemplate& t) {
    auto* go = scene.CreateGameObject(name);
    go->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
    VsAddTemplateComponents(go, t.add);
    auto* vs = go->AddComponent<VisualScriptComponent>();
    std::string err;
    CHECK(vs->LoadFromText(t.graph, &err));
    if (!err.empty()) std::printf("  %s: %s\n", t.name, err.c_str());
    return vs;
}

void Keys(std::initializer_list<char> down) { Input::FeedKeys(std::vector<char>(down)); }

const VsTemplate& T(const char* name) {
    const VsTemplate* t = VsTemplateNamed(name);
    CHECK(t != nullptr);
    return *t;
}

} // namespace

void EnsureExamplePrefabs() {
    namespace fs = std::filesystem;
    fs::create_directories("examples/node_playground");
    auto save = [](const char* path, auto&& build) {
        if (fs::exists(path)) return;
        Scene scene("Prefab");
        GameObject* go = build(scene);
        CHECK(SceneSerializer::SaveObjectToFile(*go, path));
    };
    save("examples/node_playground/marker.okayprefab", [](Scene& scene) {
        GameObject* go = scene.CreateGameObject("Marker");
        auto* mesh = go->AddComponent<MeshRenderer>();
        mesh->mesh = Mesh::Cube();
        mesh->color = Color{0.95f, 0.85f, 0.25f, 1.0f};
        return go;
    });
    save("examples/node_playground/shot.okayprefab", [](Scene& scene) {
        GameObject* go = scene.CreateGameObject("Shot");
        auto* mesh = go->AddComponent<MeshRenderer>();
        mesh->mesh = Mesh::Cube();
        mesh->color = Color{0.95f, 0.35f, 0.25f, 1.0f};
        go->transform->localScale = {0.35f, 0.35f, 0.35f};
        go->AddComponent<VisualScriptComponent>()->LoadFromText(
            "node 1 OnUpdate\n"
            "node 2 DeltaTime\n"
            "node 3 Const 6\n"
            "node 4 Mul\n"
            "node 5 Translate\n"
            "data 4 0 3 0\n"
            "data 4 1 2 0\n"
            "data 5 0 4 0\n"
            "default 5 1 0\n"
            "default 5 2 0\n"
            "exec 1 0 5\n"
            "entry OnUpdate 1\n");
        return go;
    });
    const char* scenePath = "examples/node_playground/variables.okayscene";
    if (fs::exists(scenePath)) return;
    Scene scene("Variables");
    auto* cam = scene.CreateGameObject("Main Camera");
    cam->transform->localPosition = {0, 2.2f, 8};
    scene.mainCamera = cam->AddComponent<Camera>();
    auto* cube = scene.CreateGameObject("Counter");
    cube->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
    cube->AddComponent<VisualScriptComponent>()->LoadFromText(
        "var score 0\n"
        "node 1 OnTimer 0.5\n"
        "entry OnTimer:0.5 1\n"
        "node 2 AddVar score\n"
        "node 3 Const 1\n"
        "data 2 0 3 0\n"
        "node 4 GetVar score\n"
        "node 5 SetSceneVar score\n"
        "data 5 0 4 0\n"
        "node 6 Const 5\n"
        "node 7 Compare <\n"
        "data 7 0 4 0\n"
        "data 7 1 6 0\n"
        "node 8 Branch\n"
        "data 8 0 7 0\n"
        "node 9 Translate\n"
        "default 9 0 0.4\n"
        "default 9 1 0\n"
        "default 9 2 0\n"
        "exec 1 0 2\n"
        "exec 2 0 5\n"
        "exec 5 0 8\n"
        "exec 8 0 9\n");
    auto* read = scene.CreateGameObject("Readout");
    read->transform->localPosition = {0, 2, 0};
    read->AddComponent<TextRenderer>()->text = "Press Play";
    read->AddComponent<VisualScriptComponent>()->LoadFromText(
        "node 1 OnUpdate\n"
        "entry OnUpdate 1\n"
        "node 2 GetSceneVar score\n"
        "node 3 ToString\n"
        "data 3 0 2 0\n"
        "node 4 Const \"Score: \"\n"
        "node 5 Concat\n"
        "data 5 0 4 0\n"
        "data 5 1 3 0\n"
        "node 6 SetText self\n"
        "data 6 0 5 0\n"
        "exec 1 0 6\n");
    CHECK(SceneSerializer::SaveToFile(scene, scenePath));
}

int main() {
#ifdef OKAY_SOURCE_DIR
    std::filesystem::current_path(OKAY_SOURCE_DIR);
#endif
    EnsureExamplePrefabs();
    RUN_SUITE("vs_templates");
    CHECK(VsTemplateCount() >= 16);
    for (const VsTemplate& t : VsTemplates()) {
        CHECK(t.name && t.name[0]);
        CHECK(t.blurb && t.blurb[0]);
        CHECK(t.category && t.category[0]);
        CHECK((int)t.level >= 0 && (int)t.level <= 2);
        CHECK(t.graph.find("comment ") != std::string::npos);
        CHECK(t.graph.find("\nvar ") != std::string::npos || t.graph.rfind("var ", 0) == 0);
        Scene scene("T");
        std::string err;
        auto* vs = scene.CreateGameObject("A")->AddComponent<VisualScriptComponent>();
        CHECK(vs->LoadFromText(t.graph, &err));
        if (!err.empty()) std::printf("  load %s: %s\n", t.name, err.c_str());
        CHECK(vs->Graph() && !vs->Graph()->ExposedVars().empty());
        for (const auto& is : okay::vs::ValidateGraphText(t.graph))
            CHECK(is.message.find("never sets it") == std::string::npos);
    }

    {
        Scene scene("Move");
        auto* vs = Attach(scene, "Mover", T("Move with WASD"));
        scene.Start();
        Keys({}); scene.Update(0.1f);
        Keys({'d'}); scene.Update(0.5f);
        CHECK(vs->gameObject->transform->localPosition.x > 0.5f);
    }
    {
        Scene scene("Spin");
        auto* vs = Attach(scene, "Spinner", T("Spin"));
        scene.Start();
        const float before = vs->gameObject->transform->LocalEulerAngles().z;
        scene.Update(0.5f);
        CHECK(std::fabs(vs->gameObject->transform->LocalEulerAngles().z - before) > 10.0f);
    }
    {
        Scene scene("Float");
        auto* vs = Attach(scene, "Floater", T("Float"));
        scene.Start();
        scene.Update(0.4f);
        CHECK(std::fabs(vs->gameObject->transform->localPosition.y) > 0.05f);
    }
    {
        Scene scene("Follow");
        auto* vs = Attach(scene, "Follower", T("Follow a target"));
        auto* player = scene.CreateGameObject("Player");
        player->transform->localPosition = {4, 0, 0};
        scene.Start();
        scene.Update(0.5f);
        CHECK(vs->gameObject->transform->localPosition.x > 0.5f);
    }
    {
        Scene scene("Patrol");
        auto* vs = Attach(scene, "Patroller", T("Patrol"));
        scene.Start();
        scene.Update(0.5f);
        CHECK(vs->gameObject->transform->localPosition.x > 0.4f);
        bool turned = false;
        for (int i = 0; i < 50 && !turned; ++i) {
            const float before = vs->gameObject->transform->localPosition.x;
            scene.Update(0.1f);
            if (vs->gameObject->transform->localPosition.x < before - 0.05f) turned = true;
        }
        CHECK(turned);
    }
    {
        Scene scene("Jump");
        auto* vs = Attach(scene, "Jumper", T("Jump"));
        auto* body = vs->gameObject->GetComponent<Rigidbody3D>();
        CHECK(body != nullptr);
        scene.Start();
        Keys({}); scene.Update(0.05f);
        Keys({' '}); scene.Update(0.05f);
        CHECK(body->velocity.y > 1.0f);
    }
    {
        Scene scene("Door");
        auto* vs = Attach(scene, "Door", T("Door"));
        scene.Start();
        vs->OnTriggerEnter3D(nullptr);
        CHECK(vs->GetVariable("open").AsFloat() == 1.0f);
        CHECK(vs->gameObject->transform->localPosition.y > 1.5f);
    }
    {
        Scene scene("Pickup");
        auto* vs = Attach(scene, "Coin", T("Pickup"));
        scene.Start();
        vs->OnTriggerEnter3D(nullptr);
        CHECK(vs->GetVariable("score").AsFloat() == 1.0f);
        CHECK(!vs->gameObject->active);
    }
    {
        Scene scene("Press");
        auto* vs = Attach(scene, "Switch", T("Press E"));
        scene.Start();
        Keys({}); scene.Update(0.05f);
        Keys({'e'}); scene.Update(0.05f);
        CHECK(vs->GetVariable("on").AsFloat() == 1.0f);
        CHECK(vs->gameObject->GetComponent<MeshRenderer>()->color.g > 0.5f);
    }
    {
        Scene scene("Health");
        auto* vs = Attach(scene, "Hero", T("Health"));
        scene.Start();
        CHECK(vs->GetVariable("health").AsFloat() == 100.0f);
        vs->gameObject->transform->localPosition = {4, 0, 0};
        vs->SendEvent("hit");
        CHECK(vs->GetVariable("health").AsFloat() == 75.0f);
        for (int i = 0; i < 3; ++i) vs->SendEvent("hit");
        CHECK(vs->GetVariable("deaths").AsFloat() >= 1.0f);
        CHECK(vs->gameObject->transform->localPosition.x < 0.1f);
        CHECK(vs->GetVariable("health").AsFloat() == 100.0f);
    }
    {
        Scene scene("Checkpoint");
        auto* vs = Attach(scene, "Flag", T("Checkpoint"));
        scene.Start();
        vs->gameObject->transform->localPosition = {3, 1, 0};
        vs->OnTriggerEnter3D(nullptr);
        CHECK(vs->GetVariable("saved").AsFloat() == 1.0f);
        CHECK_NEAR(vs->GetVariable("cx").AsFloat(), 3.0f, 0.01f);
        vs->gameObject->transform->localPosition = {9, 0, 0};
        vs->SendEvent("respawn");
        CHECK_NEAR(vs->gameObject->transform->localPosition.x, 3.0f, 0.01f);
        CHECK_NEAR(vs->gameObject->transform->localPosition.y, 1.0f, 0.01f);
    }
    {
        Scene scene("Platform");
        auto* vs = Attach(scene, "Platform", T("Moving platform"));
        scene.Start();
        scene.Update(0.5f);
        CHECK(vs->gameObject->transform->localPosition.x > 0.4f);
    }
    {
        Scene scene("Win");
        auto* vs = Attach(scene, "Goal", T("Win at score"));
        scene.Start();
        vs->SetVariable("score", 10.0f);
        scene.Update(0.1f);
        CHECK(vs->GetVariable("won").AsFloat() == 1.0f);
    }
    {
        Scene scene("Chase");
        auto* vs = Attach(scene, "Enemy", T("Chase"));
        scene.CreateGameObject("Player")->transform->localPosition = {5, 0, 0};
        scene.Start();
        scene.Update(0.5f);
        CHECK(vs->gameObject->transform->localPosition.x > 0.5f);
    }
    {
        Scene scene("Count");
        auto* vs = Attach(scene, "Clock", T("Countdown"));
        auto* text = vs->gameObject->GetComponent<TextRenderer>();
        CHECK(text != nullptr);
        scene.Start();
        for (int i = 0; i < 6; ++i) scene.Update(1.0f);
        CHECK(vs->GetVariable("won").AsFloat() == 1.0f);
        CHECK(text->text.find("win") != std::string::npos);
    }
    {
        Scene scene("Score");
        auto* vs = Attach(scene, "Hud", T("Score readout"));
        scene.Start();
        vs->SetVariable("score", 4.0f);
        scene.Update(0.1f);
        CHECK(vs->gameObject->GetComponent<TextRenderer>()->text.find("4") != std::string::npos);
    }
    {
        Scene scene("Message");
        auto* vs = Attach(scene, "Sign", T("Message"));
        scene.Start();
        CHECK(vs->gameObject->GetComponent<TextRenderer>()->text == "Hello");
    }
    {
        Scene scene("Hit");
        auto* camGo = scene.CreateGameObject("Camera");
        scene.mainCamera = camGo->AddComponent<Camera>();
        auto* vs = Attach(scene, "Target", T("Hit feedback"));
        scene.Start();
        vs->SendEvent("hit");
        CHECK(vs->GetVariable("hits").AsFloat() == 1.0f);
        CHECK(vs->gameObject->GetComponent<AudioSource>()->IsPlaying());
        CHECK(vs->gameObject->GetComponent<ParticleSystem>()->AliveCount() > 0);
        CHECK(scene.mainCamera->ShakeTrauma() > 0.0f);
    }
    {
        Scene scene("Shake");
        auto* camGo = scene.CreateGameObject("Camera");
        scene.mainCamera = camGo->AddComponent<Camera>();
        Attach(scene, "Boom", T("Camera shake"));
        scene.Start();
        CHECK(scene.mainCamera->ShakeTrauma() > 0.0f);
    }
    {
        Scene scene("FollowCam");
        auto* camGo = scene.CreateGameObject("Camera");
        scene.mainCamera = camGo->AddComponent<Camera>();
        scene.CreateGameObject("Player");
        Attach(scene, "Rig", T("Camera follow"));
        scene.Start();
        auto* follow = camGo->GetComponent<CameraFollow>();
        CHECK(follow != nullptr);
        CHECK(follow->targetName == "Player");
    }
    {
        Scene scene("Zoom");
        auto* camGo = scene.CreateGameObject("Camera");
        auto* cam = camGo->AddComponent<Camera>();
        scene.mainCamera = cam;
        const float before = cam->fieldOfView;
        Attach(scene, "Zoom", T("Camera zoom"));
        scene.Start();
        Keys({}); scene.Update(0.05f);
        Keys({'c'}); scene.Update(0.05f);
        CHECK(cam->fieldOfView < before);
        CHECK_NEAR(cam->fieldOfView, 30.0f, 0.1f);
    }
    {
        Scene scene("Cool");
        auto* vs = Attach(scene, "Gun", T("Cooldown ability"));
        scene.Start();
        Keys({}); scene.Update(0.05f);
        Keys({' '}); scene.Update(0.05f);
        CHECK(vs->GetVariable("uses").AsFloat() == 1.0f);
        Keys({}); scene.Update(0.05f);
        Keys({' '}); scene.Update(0.05f);
        CHECK(vs->GetVariable("uses").AsFloat() == 1.0f);
        scene.Update(1.0f);
        Keys({}); scene.Update(0.05f);
        Keys({' '}); scene.Update(0.05f);
        CHECK(vs->GetVariable("uses").AsFloat() == 2.0f);
    }
    {
        Scene scene("State");
        auto* vs = Attach(scene, "Guard", T("State machine"));
        auto* player = scene.CreateGameObject("Player");
        player->transform->localPosition = {10, 0, 0};
        scene.Start();
        scene.Update(0.2f);
        CHECK(vs->GetVariable("state").AsFloat() == 0.0f);
        player->transform->localPosition = {1, 0, 0};
        scene.Update(0.2f);
        CHECK(vs->GetVariable("state").AsFloat() == 1.0f);
        CHECK(vs->gameObject->transform->localPosition.x > 0.1f);
    }
    {
        Scene scene("Spawn");
        auto* vs = Attach(scene, "Spawner", T("Spawner"));
        const int before = (int)scene.Objects().size();
        scene.Start();
        scene.Update(1.05f);
        CHECK(vs->GetVariable("made").AsFloat() >= 1.0f);
        CHECK((int)scene.Objects().size() > before);
    }
    {
        Scene scene("Shoot");
        auto* vs = Attach(scene, "Gun", T("Shooter"));
        scene.Start();
        Keys({}); scene.Update(0.05f);
        Keys({' '}); scene.Update(0.05f);
        CHECK(vs->GetVariable("shots").AsFloat() == 1.0f);
        Keys({}); scene.Update(0.05f);
        Keys({' '}); scene.Update(0.05f);
        CHECK(vs->GetVariable("shots").AsFloat() == 1.0f);
        bool flew = false;
        Keys({});
        for (int i = 0; i < 8; ++i) scene.Update(0.1f);
        for (const auto& up : scene.Objects())
            if (up && up->name == "Shot" && up->transform->localPosition.x > 2.0f) flew = true;
        CHECK(flew);
        Keys({}); scene.Update(0.05f);
        Keys({' '}); scene.Update(0.05f);
        CHECK(vs->GetVariable("shots").AsFloat() == 2.0f);
    }
    {
        Scene scene("Sight");
        auto* vs = Attach(scene, "Enemy", T("Line of sight"));
        auto* player = scene.CreateGameObject("Player");
        player->transform->localPosition = {4, 0, 0};
        player->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        player->AddComponent<BoxCollider3D>();
        scene.Start();
        scene.Update(0.3f);
        CHECK(vs->GetVariable("seen").AsFloat() == 1.0f);
        CHECK(vs->gameObject->transform->localPosition.x > 0.2f);
    }
    {
        Scene scene("Wall");
        auto* vs = Attach(scene, "Enemy", T("Line of sight"));
        auto* player = scene.CreateGameObject("Player");
        player->transform->localPosition = {4, 0, 0};
        player->AddComponent<BoxCollider3D>();
        auto* wall = scene.CreateGameObject("Wall");
        wall->transform->localPosition = {2, 0, 0};
        wall->AddComponent<BoxCollider3D>()->size = {1, 2, 1};
        scene.Start();
        scene.Update(0.3f);
        CHECK(vs->GetVariable("seen").AsFloat() == 0.0f);
        CHECK(vs->gameObject->transform->localPosition.x < 0.05f);
    }
    {
        Scene scene("Bar");
        auto* vs = Attach(scene, "Hero", T("Health bar"));
        auto* bar = vs->gameObject->GetComponent<UIProgressBar>();
        CHECK(bar != nullptr);
        scene.Start();
        scene.Update(0.05f);
        CHECK_NEAR(bar->value, 1.0f, 0.01f);
        vs->SendEvent("hit");
        scene.Update(0.05f);
        CHECK(vs->GetVariable("health").AsFloat() == 75.0f);
        CHECK_NEAR(bar->value, 0.75f, 0.01f);
    }
    {
        UICanvas::Set(200, 200);
        Scene scene("Click");
        auto* camGo = scene.CreateGameObject("Camera");
        camGo->transform->localPosition = {4, 10, 2};
        // Cameras look down their -Z. LookAt aims +Z, so aim +Z upward and the view looks down.
        camGo->transform->LookAt({4, 20, 2});
        scene.mainCamera = camGo->AddComponent<Camera>();
        auto* vs = Attach(scene, "Walker", T("Click to move"));
        vs->gameObject->transform->localPosition = {0, 0, 2};
        scene.Start();
        Input::FeedMouse({100, 100}, 0);
        scene.Update(0.05f);
        Input::FeedMouse({100, 100}, 1);
        scene.Update(0.05f);
        Input::FeedMouse({100, 100}, 0);
        for (int i = 0; i < 8; ++i) scene.Update(0.1f);
        CHECK(vs->gameObject->transform->localPosition.x > 1.0f);
    }
    {
        Scene scene("Cams");
        auto* wideGo = scene.CreateGameObject("Wide");
        wideGo->transform->localPosition = {0, 4, 10};
        auto* wide = wideGo->AddComponent<Camera>();
        wide->main = true;
        scene.mainCamera = wide;
        auto* closeGo = scene.CreateGameObject("Close");
        closeGo->transform->localPosition = {0, 1, 3};
        closeGo->AddComponent<Camera>();
        Attach(scene, "Switch", T("Camera switch"));
        scene.Start();
        Keys({}); scene.Update(0.05f);
        Keys({'c'}); scene.Update(0.05f);
        CHECK(scene.mainCamera != nullptr);
        CHECK(scene.mainCamera->gameObject->name == "Close");
    }
    {
        Scene scene("Outcome");
        auto* vs = Attach(scene, "Board", T("Win or lose"));
        auto* text = vs->gameObject->GetComponent<TextRenderer>();
        scene.Start();
        vs->SetVariable("score", 10.0f);
        scene.Update(0.1f);
        CHECK(vs->GetVariable("won").AsFloat() == 1.0f);
        CHECK(text->text.find("win") != std::string::npos);
    }
    {
        Scene scene("Defeat");
        auto* vs = Attach(scene, "Board", T("Win or lose"));
        scene.Start();
        vs->SetVariable("health", 0.0f);
        scene.Update(0.1f);
        CHECK(vs->GetVariable("lost").AsFloat() == 1.0f);
        CHECK(vs->gameObject->GetComponent<TextRenderer>()->text.find("lose") != std::string::npos);
    }
    {
        Scene scene("DoorPhys");
        auto* door = Attach(scene, "Door", T("Door"));
        auto* guest = scene.CreateGameObject("Guest");
        guest->transform->localPosition = {-1.6f, 0, 0};
        guest->AddComponent<BoxCollider3D>();
        auto* body = guest->AddComponent<Rigidbody3D>();
        body->bodyType = Rigidbody3D::BodyType::Kinematic;
        body->gravityScale = 0.0f;
        body->velocity = {3, 0, 0};
        scene.Start();
        for (int i = 0; i < 30 && door->GetVariable("open").AsFloat() < 1.0f; ++i) scene.Update(0.05f);
        CHECK(door->GetVariable("open").AsFloat() == 1.0f);
        CHECK(door->gameObject->transform->localPosition.y > 1.5f);
    }
    {
        Scene scene("Vars");
        std::string err;
        CHECK(SceneSerializer::LoadFromFile(scene, "examples/node_playground/variables.okayscene", &err));
        if (!err.empty()) std::printf("  playground: %s\n", err.c_str());
        scene.Start();
        for (int i = 0; i < 8; ++i) scene.Update(0.5f);
        GameObject* counter = nullptr;
        GameObject* read = nullptr;
        for (const auto& up : scene.Objects()) {
            if (up->name == "Counter") counter = up.get();
            if (up->name == "Readout") read = up.get();
        }
        CHECK(counter != nullptr);
        auto* vs = counter->GetComponent<VisualScriptComponent>();
        CHECK(vs->GetVariable("score").AsFloat() >= 4.0f);
        CHECK(counter->transform->localPosition.x > 1.0f);
        auto* label = read->GetComponent<TextRenderer>();
        CHECK(label->text.find("Score:") != std::string::npos);
    }
    Keys({});
    Input::FeedMouse({0, 0}, 0);
    TEST_MAIN_RESULT();
}
