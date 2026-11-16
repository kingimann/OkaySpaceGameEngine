// The player obstacle course (examples/player/player_obstacle_course.okayscene): built here in code with the scene
// writer, so the file in the examples folder is always what this program makes. Run it with
//   OKAY_WRITE_COURSE=<path> test_player_course        to (re)write the scene file
// and it checks, without that, that the shipped file is byte for byte this scene, then loads the file and plays a
// player through every station headless (the same steps the owner walks in docs/try_it.md row 7w).
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

namespace {

GameObject* Slab(Scene& s, const char* name, Vec3 c, Vec3 size, Color col, Quat rot = Quat::Identity) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = c;
    g->transform->localRotation = rot;
    g->transform->localScale = size;
    auto* mr = g->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube(1.0f);
    mr->color = col;
    g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};   // the unit cube, scaled by the transform
    return g;
}

void Label(Scene& s, const char* text, Vec3 at) {
    GameObject* g = s.CreateGameObject("Label");
    g->transform->localPosition = at;
    auto* t = g->AddComponent<TextRenderer>();
    t->text = text;
    t->pixelSize = 0.045f;
    t->color = Color::FromBytes(255, 236, 160);
    t->outline = true;
}

const Color kFloor = Color::FromBytes(120, 124, 132), kRamp = Color::FromBytes(190, 150, 90), kWall = Color::FromBytes(150, 90, 90),
            kMove = Color::FromBytes(90, 150, 200);

// Stations run along -Z, 16 m apart. The player starts at z = +6 facing -Z.
void BuildCourse(Scene& s) {
    s.SetName("Player obstacle course");
    s.CreateGameObject("Directional Light")->AddComponent<Light>();
    Slab(s, "Ground", {0, -0.5f, -100}, {60, 1, 260}, kFloor);

    Label(s, "START: WASD walk, Shift run, Space jump, Ctrl crouch, V switch view, F3 shows the player numbers", {-9, 3.2f, 3});

    // 1. Ramps (z = -10): 15, 30, 45, 55, 65 degrees. Slope limit is 50: the first three walk, the last two slide you back.
    Label(s, "1 RAMPS 15 30 45 | 55 65 are too steep: you slide back, standing still on the others does not creep", {-12, 5.5f, -4});
    const float angles[] = {15, 30, 45, 55, 65};
    for (int i = 0; i < 5; ++i) {
        const float a = angles[i], th = a * 0.0174532925f, len = 6.0f;
        const float x = -10.0f + i * 5.0f;
        Slab(s, "Ramp", {x, std::sin(th) * len * 0.5f - 0.15f, -13.0f - std::cos(th) * len * 0.5f}, {3.5f, 0.3f, len},
             kRamp, Quat::AngleAxis(a, Vec3::Right));
    }

    // 2. Stairs up and down (z = -32): 12 steps of 0.18 m. Walking down stays on the treads (no hopping).
    Label(s, "2 STAIRS: up and down, the camera and feet stay calm", {-5, 4.0f, -28});
    for (int i = 0; i < 12; ++i)
        Slab(s, "Tread", {0, (i + 1) * 0.18f * 0.5f, -32.0f - (i + 0.5f) * 0.3f}, {3, (i + 1) * 0.18f, 0.3f}, kRamp);
    Slab(s, "Stair landing", {0, 2.16f * 0.5f, -32.0f - 3.6f - 2.5f}, {5, 2.16f, 5}, kRamp);

    // 3. Step heights (z = -48): 0.10, 0.25, 0.35 are climbed, 0.6 stops you.
    Label(s, "3 STEPS 0.10 0.25 0.35 climb, 0.60 blocks", {-6, 3.0f, -46});
    const float hs[] = {0.10f, 0.25f, 0.35f, 0.60f};
    for (int i = 0; i < 4; ++i) Slab(s, "Step", {-6.0f + i * 4.0f, hs[i] * 0.5f, -50.0f}, {2.5f, hs[i], 2.5f}, kRamp);

    // 4. Ledge and gap (z = -62): a plank across a pit.
    Label(s, "4 LEDGE: a plank over a gap, step off the side", {-6, 3.0f, -58});
    Slab(s, "Plank", {0, -0.1f, -64.0f}, {1.2f, 0.2f, 10}, kRamp);

    // 5. Low tunnel (z = -78): crouch to enter, the stand-up is refused under the roof.
    Label(s, "5 TUNNEL 1.3 m: crouch (Ctrl) to go in; standing up inside is refused", {-7, 3.0f, -73});
    Slab(s, "Tunnel roof", {0, 1.6f, -80.0f}, {4, 0.6f, 8}, kWall);
    Slab(s, "Tunnel wall L", {-2.2f, 0.9f, -80.0f}, {0.4f, 1.8f, 8}, kWall);
    Slab(s, "Tunnel wall R", {2.2f, 0.9f, -80.0f}, {0.4f, 1.8f, 8}, kWall);

    // 6. Moving platform and a turning bridge (z = -94).
    Label(s, "6 MOVING: ride the platform (it slides), jump off keeps its speed; the bridge turns", {-9, 4.0f, -90});
    {
        GameObject* pl = Slab(s, "Moving platform", {-8, 0.15f, -96.0f}, {4, 0.3f, 4}, kMove);
        auto* tw = pl->AddComponent<TweenAnimation>();
        tw->type = (int)TweenAnimation::Type::Move; tw->endValue = {8, 0.15f, -96.0f}; tw->duration = 4.0f;
        tw->ease = (int)Ease::Linear; tw->loops = -1; tw->loopType = (int)LoopType::Yoyo;
        GameObject* br = Slab(s, "Turning bridge", {0, 0.4f, -104.0f}, {8, 0.3f, 1.6f}, kMove);
        br->AddComponent<Spinner>()->angularVelocity = {0, 30, 0};
    }

    // 7. Thin wall (z = -116): run at it; you stop.
    Label(s, "7 THIN WALL 2 cm: hold Shift and run into it, you must not get through", {-8, 3.0f, -112});
    Slab(s, "Thin wall", {0, 1.5f, -118.0f}, {10, 3, 0.02f}, kWall);

    // 8. Shallow and deep water (z = -132).
    Label(s, "8 WATER: wade in the shallow end, swim in the deep end (Space up, Ctrl down)", {-8, 3.0f, -126});
    {
        GameObject* w = s.CreateGameObject("Water");
        w->transform->localPosition = {0, 0, -134.0f};
        auto* wa = w->AddComponent<Water>();
        wa->size = 12; wa->depth = 2.2f; wa->waveHeight = 0.05f; wa->resolution = 12;
        Slab(s, "Pool floor deep", {0, -2.45f, -137.0f}, {12, 0.5f, 6}, kFloor);
        Slab(s, "Pool floor shallow", {0, -0.55f, -131.0f}, {12, 0.5f, 6}, kFloor);
        Slab(s, "Pool wall", {0, 0.3f, -140.4f}, {12, 1.0f, 0.4f}, kFloor);
    }

    // 9. A patch of voxel terrain (z = -152).
    Label(s, "9 VOXEL TERRAIN: walk on the hill and the little hole", {-8, 4.0f, -146});
    {
        GameObject* v = s.CreateGameObject("Voxel patch");
        v->transform->localPosition = {0, 0, -156.0f};
        auto* vt = v->AddComponent<VoxelTerrain>();
        vt->Resize(24, 14, 24);
        vt->voxelSize = 0.8f;
        vt->FillSlab(0.12f);
        vt->Add({0, 1.4f, 0}, 3.0f, 1.0f);       // a hill
        vt->Dig({5.0f, 1.0f, 3.0f}, 1.4f, 1.0f); // a small hole
    }

    // The player and its camera: a third-person player (V switches to the eyes).
    GameObject* player = Templates::AddThirdPersonPlayer(s, {0, 0.05f, 6});
    player->name = "Player";
}

// Object ids are random each run; everything else is the same every time.
std::string WithoutGuids(const std::string& t) {
    std::istringstream in(t); std::string line, out;
    while (std::getline(in, line)) if (line.find("guid \"") == std::string::npos) out += line + "\n";
    return out;
}

std::string Text() {
    Scene s("course");
    BuildCourse(s);
    return SceneSerializer::Serialize(s);
}

} // namespace

int main() {
    RUN_SUITE("player_course");
    const std::string text = Text();
    CHECK(text.size() > 1000);
    if (const char* out = std::getenv("OKAY_WRITE_COURSE")) {
        std::ofstream f(out, std::ios::binary);
        f << text;
        std::printf("wrote %s (%zu bytes)\n", out, text.size());
        return 0;
    }
#ifdef OKAY_SOURCE_DIR
    const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/player/player_obstacle_course.okayscene";
    std::ifstream in(path, std::ios::binary);
    CHECK(in.good());
    std::stringstream ss; ss << in.rdbuf();
    CHECK(WithoutGuids(ss.str()) == WithoutGuids(text));   // the file in examples/ is what BuildCourse writes (ids aside)

    Scene sc("play");
    sc.physicsEnabled = true;
    sc.UseFixedStepPhysics(true);
    CHECK(SceneSerializer::LoadFromFile(sc, path));
    GameObject* p = sc.Find("Player");
    CHECK(p != nullptr);
    if (p) {
        sc.Start();
        auto frames = [&](double sec, std::vector<char> k, float yaw) {
            if (auto* fp = p->GetComponent<FirstPersonController>()) fp->yaw = yaw;
            if (auto* tp = p->GetComponent<ThirdPersonController>()) tp->yaw = yaw;
            for (int i = 0, n = (int)std::lround(sec * 60.0); i < n; ++i) { Input::FeedKeys(k); sc.Update(1.0f / 60.0f); }
            Input::FeedKeys({});
        };
        frames(1.0, {}, 0.0f);
        CHECK(p->transform->Position().y < 0.2f);
        // Station 3: walk the 0.25 m step line (x = -2): up and over, still alive.
        p->transform->SetPosition({-2.0f, 0.05f, -45.0f});
        frames(2.0, {'w'}, 0.0f);
        CHECK(p->transform->Position().z < -52.0f);
        // Station 7: sprint at the 2 cm wall.
        p->transform->SetPosition({0, 0.05f, -110.0f});
        frames(3.0, {'w', Input::KeyShift}, 0.0f);
        CHECK(p->transform->Position().z > -118.2f);
        // Station 1: stand on the 30 degree ramp, no creep.
        p->transform->SetPosition({-5.0f, 1.6f, -15.6f});
        frames(1.0, {}, 0.0f);
        const Vec3 a = p->transform->Position();
        frames(2.0, {}, 0.0f);
        const Vec3 b = p->transform->Position();
        CHECK(std::sqrt((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z)) < 0.1f);
    }
#endif
    TEST_MAIN_RESULT();
}
