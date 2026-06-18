#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("serialize");

    // Build a scene with hierarchy, a camera, and a sprite.
    Scene scene("Level1");
    GameObject* cam = scene.CreateGameObject("MainCamera");
    auto* c = cam->AddComponent<Camera>();
    c->orthographicSize = 9.0f;

    GameObject* parent = scene.CreateGameObject("Parent");
    parent->transform->localPosition = {3, 4, 0};
    parent->tag = "player";

    GameObject* child = scene.CreateGameObject("Child");
    child->transform->SetParent(parent->transform);
    child->transform->localPosition = {1, 0, 0};
    auto* sr = child->AddComponent<SpriteRenderer>();
    sr->glyph = 'Z';
    sr->color = Color::Red;
    sr->size  = {2.0f, 3.0f};

    std::string text = SceneSerializer::Serialize(scene);
    CHECK(!text.empty());

    // Round-trip into a fresh scene.
    Scene loaded("tmp");
    std::string err;
    bool ok = SceneSerializer::Deserialize(loaded, text, &err);
    CHECK(ok);
    if (!ok) std::cerr << "  deserialize error: " << err << "\n";

    CHECK(loaded.Name() == "Level1");
    CHECK(loaded.Objects().size() == 3);

    GameObject* lc = loaded.Find("Child");
    GameObject* lp = loaded.Find("Parent");
    CHECK(lc != nullptr);
    CHECK(lp != nullptr);
    if (lc && lp) {
        // Hierarchy preserved: child world pos = parent(3,4) + child local(1,0).
        CHECK(lc->transform->Parent() == lp->transform);
        CHECK(lc->transform->Position() == Vec3(4, 4, 0));
        CHECK(lp->tag == "player");
        auto* lsr = lc->GetComponent<SpriteRenderer>();
        CHECK(lsr != nullptr);
        if (lsr) {
            CHECK(lsr->glyph == 'Z');
            CHECK_NEAR(lsr->size.x, 2.0f, 0.001f);
            CHECK_NEAR(lsr->color.r, 1.0f, 0.001f);
        }
    }
    GameObject* lcam = loaded.Find("MainCamera");
    CHECK(lcam != nullptr);
    if (lcam) {
        auto* cc = lcam->GetComponent<Camera>();
        CHECK(cc != nullptr);
        if (cc) CHECK_NEAR(cc->orthographicSize, 9.0f, 0.001f);
    }

    TEST_MAIN_RESULT();
}
