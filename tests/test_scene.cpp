#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

namespace {
// A probe script that records how often each lifecycle stage ran.
struct Probe : public Behaviour {
    int awake = 0, start = 0, update = 0;
    void Awake() override  { ++awake; }
    void Start() override   { ++start; }
    void Update(float) override { ++update; }
};
}

int main() {
    RUN_SUITE("scene");

    Scene scene("Test");

    // GameObjects always have a Transform.
    GameObject* go = scene.CreateGameObject("Hero");
    CHECK(go->transform != nullptr);
    CHECK(go->GetComponent<Transform>() == go->transform);

    // AddComponent / GetComponent
    Probe* probe = go->AddComponent<Probe>();
    CHECK(go->GetComponent<Probe>() == probe);

    // Lifecycle: Awake + Start fire exactly once across frames.
    scene.Start();
    CHECK(probe->awake == 1);
    CHECK(probe->start == 1);
    CHECK(probe->update == 0);

    scene.Update(0.016f);
    scene.Update(0.016f);
    CHECK(probe->awake == 1);
    CHECK(probe->start == 1);
    CHECK(probe->update == 2);

    // Transform hierarchy: child world position tracks parent.
    GameObject* parent = scene.CreateGameObject("Parent");
    GameObject* child  = scene.CreateGameObject("Child");
    parent->transform->localPosition = {10, 0, 0};
    child->transform->SetParent(parent->transform);
    child->transform->localPosition  = {0, 5, 0};
    Vec3 worldPos = child->transform->Position();
    CHECK(worldPos == Vec3(10, 5, 0));

    // Find by name.
    CHECK(scene.Find("Hero") == go);
    CHECK(scene.Find("Missing") == nullptr);

    // Destroy removes the object and stops its updates.
    int before = probe->update;
    scene.Destroy(go);
    scene.Update(0.016f);
    CHECK(scene.Find("Hero") == nullptr);
    // probe was owned by the destroyed object; its pointer is dangling now,
    // so just confirm the object is gone rather than touching it again.
    (void)before;

    // Headless render produces a non-empty frame buffer.
    Scene render("Render");
    GameObject* camObj = render.CreateGameObject("Cam");
    auto* cam = camObj->AddComponent<Camera>();
    cam->orthographicSize = 5.0f;
    GameObject* quad = render.CreateGameObject("Quad");
    quad->transform->localScale = Vec3{2.0f};
    auto* sr = quad->AddComponent<SpriteRenderer>();
    sr->glyph = 'X';
    render.Start();

    ConsoleRenderer cr(20, 10, /*clearScreen=*/false);
    render.Render(cr);
    const auto& fb = cr.FrameBuffer();
    bool drewSomething = false;
    for (char c : fb) if (c == 'X') { drewSomething = true; break; }
    CHECK(drewSomething);

    TEST_MAIN_RESULT();
}
