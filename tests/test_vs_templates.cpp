// Every visual-script template loads, and each one changes a real scene.
#include "test_framework.hpp"
#include "VisualScriptTemplates.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include <Okay.hpp>
#include <cmath>
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

int main() {
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
    Keys({});
    TEST_MAIN_RESULT();
}
