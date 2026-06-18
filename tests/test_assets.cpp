#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>

using namespace okay;

static bool Has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

int main() {
    RUN_SUITE("assets");

    Scene scene("A");

    GameObject* p = scene.CreateGameObject("Player");
    p->AddComponent<SpriteRenderer>()->texture = "hero.png";
    p->AddComponent<AudioSource>()->clipPath = "jump.wav";

    GameObject* anim = scene.CreateGameObject("Anim");
    anim->AddComponent<SpriteRenderer>();
    auto* a = anim->AddComponent<SpriteAnimator>();
    a->frames = {"walk_0.png", "walk_1.png"};

    GameObject* plain = scene.CreateGameObject("Plain");
    plain->AddComponent<SpriteRenderer>(); // no texture -> contributes nothing

    auto assets = SceneSerializer::CollectAssetPaths(scene);
    CHECK(Has(assets, "hero.png"));
    CHECK(Has(assets, "jump.wav"));
    CHECK(Has(assets, "walk_0.png"));
    CHECK(Has(assets, "walk_1.png"));
    CHECK(assets.size() == 4); // empty texture excluded, no duplicates

    // --- Duplicate references collapse to one entry ---
    {
        Scene s2("D");
        s2.CreateGameObject("A")->AddComponent<SpriteRenderer>()->texture = "shared.png";
        s2.CreateGameObject("B")->AddComponent<SpriteRenderer>()->texture = "shared.png";
        auto a2 = SceneSerializer::CollectAssetPaths(s2);
        CHECK(a2.size() == 1);
        CHECK(a2[0] == "shared.png");
    }

    TEST_MAIN_RESULT();
}
