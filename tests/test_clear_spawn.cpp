// The default 3D scene's cube and sphere sit on either side of the origin.
// A third-person player must not be created there, and a player who is
// dragged back between them must ease out and still walk away.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/ClearSpawn.hpp"
#include "okay/Physics/ColliderFit.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/Mesh.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace okay;

static GameObject* Box(Scene& sc, const char* name, const Vec3& pos, const Vec3& size) {
    GameObject* g = sc.CreateGameObject(name);
    g->transform->localPosition = pos;
    g->AddComponent<BoxCollider3D>()->size = size;
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return g;
}

static void DefaultProps(Scene& sc) {
    Box(sc, "Ground", {0, -0.5f, 0}, {80, 1, 80});
    GameObject* cube = sc.CreateGameObject("Cube");
    cube->transform->localPosition = {-1.1f, 0.5f, 0.0f};
    auto* cm = cube->AddComponent<MeshRenderer>();
    cm->mesh = Mesh::RoundedBox();
    auto* cc = cube->AddComponent<BoxCollider3D>();
    cc->autoFit = true;
    FitColliders(cube);
    GameObject* ball = sc.CreateGameObject("Sphere");
    ball->transform->localPosition = {0.9f, 0.55f, 0.4f};
    ball->transform->localScale = {1.1f, 1.1f, 1.1f};
    auto* sm = ball->AddComponent<MeshRenderer>();
    sm->mesh = Mesh::Sphere(0.5f, 24, 32);
    auto* scoll = ball->AddComponent<BoxCollider3D>();
    scoll->autoFit = true;
    FitColliders(ball);
    Vec3 mn, mx;
    cc->WorldAABB(mn, mx);
    std::printf("  cube aabb (%.2f..%.2f, %.2f..%.2f, %.2f..%.2f)\n", mn.x, mx.x, mn.y, mx.y, mn.z, mx.z);
    scoll->WorldAABB(mn, mx);
    std::printf("  sphere aabb (%.2f..%.2f, %.2f..%.2f, %.2f..%.2f)\n", mn.x, mx.x, mn.y, mx.y, mn.z, mx.z);
}

static int SolidsAt(Scene& sc, GameObject* player) {
    auto* box = player->GetComponent<BoxCollider3D>();
    if (!box) return 0;
    const Vec3 half = box->HalfExtents();
    const Vec3 center = player->transform->Position() + box->offset + Vec3{0, 0.03f, 0};
    const Vec3 test{std::fmax(0.05f, half.x - 0.02f), std::fmax(0.05f, half.y - 0.04f),
                    std::fmax(0.05f, half.z - 0.02f)};
    Physics3D::IgnoreHierarchy own(player);
    auto hits = sc.physics3D().OverlapBox(sc, center, test, player->transform->Rotation(),
                                           Physics3D::kAllLayers, false, player);
    int n = 0;
    for (Collider3D* c : hits) {
        if (!c || c->isTrigger || !c->gameObject) continue;
        if (c->gameObject->name == "Ground") continue;
        ++n;
    }
    return n;
}

static bool BetweenProps(const Vec3& p) {
    return p.x > -0.55f && p.x < 0.45f && p.z > -0.45f && p.z < 0.7f;
}

int main() {
    RUN_SUITE("clear_spawn");

    {
        Scene sc("spawn");
        DefaultProps(sc);
        GameObject* player = Templates::AddThirdPersonPlayer(sc, {0.0f, 1.0f, 0.0f});
        CHECK(BetweenProps(player->transform->Position()));
        const Vec3 placed = ClearFeetPosition(sc, player, Vec3::Zero);
        player->transform->SetPosition(placed);
        std::printf("  spawn landed at (%.2f, %.2f, %.2f) overlaps %d\n",
                    placed.x, placed.y, placed.z, SolidsAt(sc, player));
        CHECK(SolidsAt(sc, player) == 0);
        CHECK(!BetweenProps(placed));
        CHECK(std::fabs(placed.y) < 0.08f);   // feet on the ground, not hovering
        // A plain cube dropped into the same scene also misses the props.
        GameObject* cube = sc.CreateGameObject("NewCube");
        cube->transform->localPosition = Vec3::Zero;
        auto* bc = cube->AddComponent<BoxCollider3D>();
        bc->size = Vec3::One;
        const Vec3 cubeAt = ClearFeetPosition(sc, cube, Vec3::Zero);
        cube->transform->SetPosition(cubeAt);
        CHECK(SolidsAt(sc, cube) == 0);
        CHECK(!BetweenProps(cubeAt));
    }

    // Dragged back between the props, then Play. He eases out (no one-frame
    // pop) and each compass key carries him clear.
    const char* keys[] = {"W", "A", "S", "D"};
    const std::vector<char> held[] = {{'w'}, {'a'}, {'s'}, {'d'}};
    for (int k = 0; k < 4; ++k) {
        Scene sc("wedge");
        DefaultProps(sc);
        GameObject* player = Templates::AddThirdPersonPlayer(sc, {0.0f, 1.0f, 0.0f});
        auto* tp = player->GetComponent<ThirdPersonController>();
        tp->lockCursor = false;
        tp->yaw = 0.0f;
        player->transform->SetPosition({0.0f, 0.0f, 0.0f});   // feet on the ground, in the slot
        sc.Start();
        const float dt = 1.0f / 60.0f;
        Input::FeedMouse({400.0f, 300.0f}, 0);
        Input::FeedKeys({});
        float maxStep = 0.0f;
        Vec3 prev = player->transform->Position();
        for (int i = 0; i < 90; ++i) {
            Input::FeedMouse({400.0f, 300.0f}, 0);
            Input::FeedKeys({});
            sc.Update(dt);
            const Vec3 now = player->transform->Position();
            const float step = std::sqrt((now.x - prev.x) * (now.x - prev.x) + (now.z - prev.z) * (now.z - prev.z));
            maxStep = std::fmax(maxStep, step);
            prev = now;
        }
        const Vec3 eased = player->transform->Position();
        const int left = SolidsAt(sc, player);
        std::printf("  idle ease %s: at (%.2f, %.2f, %.2f) overlaps %d max step %.3f m\n",
                    keys[k], eased.x, eased.y, eased.z, left, maxStep);
        CHECK(left == 0);
        CHECK(!BetweenProps(eased));
        CHECK(maxStep < 0.08f);

        const Vec3 from = eased;
        for (int i = 0; i < 90; ++i) {
            Input::FeedMouse({400.0f, 300.0f}, 0);
            Input::FeedKeys(held[k]);
            sc.Update(dt);
            const Vec3 now = player->transform->Position();
            const float step = std::sqrt((now.x - prev.x) * (now.x - prev.x) + (now.z - prev.z) * (now.z - prev.z));
            maxStep = std::fmax(maxStep, step);
            prev = now;
        }
        const Vec3 end = player->transform->Position();
        const float moved = (end - from).Magnitude();
        std::printf("  walk %s: moved %.2f m to (%.2f, %.2f) max step %.3f\n",
                    keys[k], moved, end.x, end.z, maxStep);
        CHECK(moved > 1.0f);
        CHECK(maxStep < 0.2f);
        CHECK(SolidsAt(sc, player) == 0);
    }

    TEST_MAIN_RESULT();
}
