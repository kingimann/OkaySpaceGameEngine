// The feel lab (examples/player/player_feel_lab.okayscene): a short course for comparing the controller presets.
// Built here in code with the scene writer, so the file in examples/ is what this program makes:
//   OKAY_WRITE_LAB=<path> test_player_feel_lab        to (re)write the scene file
// Without that it checks the shipped file is this scene (object ids aside), loads it, and plays it headless: the
// 1-5 keys switch the preset and the sign changes, the run track is as long as the sign says, a gap one preset can
// clear and another cannot is really there. The owner steps are docs/try_it.md row 7wa.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ControllerPresets.hpp"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

namespace {

GameObject* Slab(Scene& s, const char* name, Vec3 c, Vec3 size, Color col) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = c;
    g->transform->localScale = size;
    auto* mr = g->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube(1.0f);
    mr->color = col;
    g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    return g;
}

GameObject* Label(Scene& s, const char* name, const char* text, Vec3 at, float px = 0.045f) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = at;
    auto* t = g->AddComponent<TextRenderer>();
    t->text = text;
    t->pixelSize = px;
    t->color = Color::FromBytes(255, 236, 160);
    t->outline = true;
    return g;
}

const Color kFloor = Color::FromBytes(120, 124, 132), kMark = Color::FromBytes(230, 200, 90), kWall = Color::FromBytes(150, 90, 90),
            kStone = Color::FromBytes(110, 150, 190), kStep = Color::FromBytes(190, 150, 90);

// The lab runs along -Z from the start (z = +6). Everything is a box, so the numbers are easy to check.
void BuildLab(Scene& s) {
    s.SetName("Player feel lab");
    s.CreateGameObject("Directional Light")->AddComponent<Light>();
    Slab(s, "Ground", {0, -0.5f, -26.5f}, {50, 1, 73}, kFloor);   // z +10 .. -63, then the gaps

    Label(s, "PresetLabel", "PRESET: Classic   (1 Classic  2 Modern  3 Arcade  4 Realistic  5 Shooter)", {-9, 3.4f, 3});
    Label(s, "Help", "FEEL LAB: press 1-5 to change the feel, F3 shows the preset and your speed, V switches view", {-9, 2.9f, 3});

    // A. Run track: a marker every 5 m. Hold W + Shift, let go at the red line (z = -40): how far you slide is the stop.
    Label(s, "Label A", "A RUN TRACK: markers every 5 m, hold W + Shift, let go at the red line and watch the stop", {-9, 3.0f, -4});
    for (int i = 1; i <= 8; ++i)
        Slab(s, "Marker", {-4.0f, 0.02f, -4.0f - i * 5.0f}, {0.8f, 0.04f, 0.3f}, kMark);
    Slab(s, "Stop line", {-4.0f, 0.03f, -44.0f}, {3.0f, 0.05f, 0.3f}, kWall);

    // B. Slalom: posts 4 m apart, alternate sides. Turn rate and steering show here.
    Label(s, "Label B", "B SLALOM: weave through the posts at a walk, then at a run", {2.0f, 3.0f, -4});
    for (int i = 0; i < 8; ++i)
        Slab(s, "Post", {i % 2 == 0 ? 3.2f : 6.8f, 0.8f, -10.0f - i * 4.0f}, {0.5f, 1.6f, 0.5f}, kStone);

    // C. Gaps: stone islands with 2, 3 and 4 m between them, over a pit (a miss falls and returns you to the start).
    Label(s, "Label C", "C GAPS 2 / 3 / 4 m: run and jump. Arcade (double jump) crosses 4 m, Realistic only 2", {-9, 3.0f, -62});
    Slab(s, "Gap start", {0, -0.25f, -66}, {6, 0.5f, 6}, kStone);
    float z = -69.0f;
    for (float gap : {2.0f, 3.0f, 4.0f}) {
        z -= gap;
        Slab(s, "Island", {0, -0.25f, z - 1.5f}, {6, 0.5f, 3}, kStone);
        z -= 3.0f;
    }
    Slab(s, "Pit floor", {0, -8.5f, -100}, {50, 1, 90}, kWall);   // a miss falls below the fall line and returns you to the start
    Slab(s, "Far ground", {0, -0.5f, -140}, {50, 1, 50}, kFloor);

    // D. Landing: a 2 m, 4 m and 8 m drop. The big ones are hard landings (F3 counts them).
    Label(s, "Label D", "D LANDINGS: step off the 2 m, 4 m and 8 m towers; the camera dips and F3 counts hard landings", {-9, 3.0f, -120});
    for (int i = 0; i < 3; ++i) {
        const float h = i == 0 ? 2.0f : i == 1 ? 4.0f : 8.0f, x = -6.0f + i * 6.0f;
        Slab(s, "Tower", {x, h * 0.5f - 0.05f, -126}, {3, h, 3}, kStep);
        const int n = (int)std::ceil(h / 0.35f);
        for (int k = 0; k < n; ++k) {   // stairs up the front of each tower, 0.35 m risers
            const float top = std::min(h, 0.35f * (k + 1));
            Slab(s, "Stair", {x, top * 0.5f - 0.05f, -124.2f + (n - 1 - k) * 0.6f}, {3, top, 0.6f}, kStep);
        }
    }

    // E. Camera corner: a narrow corridor with a turn, for the third-person spring arm.
    Label(s, "Label E", "E CORRIDOR: walk through in third person; the camera pulls in at the walls and eases back out", {-9, 3.0f, -138});
    Slab(s, "Wall L", {-2.5f, 1.5f, -150}, {0.4f, 3, 16}, kWall);
    Slab(s, "Wall R", {2.5f, 1.5f, -150}, {0.4f, 3, 16}, kWall);
    Slab(s, "Roof", {0, 3.1f, -150}, {5.4f, 0.3f, 16}, kWall);

    GameObject* player = Templates::AddThirdPersonPlayer(s, {0, 0.05f, 6});
    player->name = "Player";
    if (auto* pv = player->GetComponent<PlayerView>()) pv->presetHotkeys = true;
    if (auto* pc = PlayerController::Of(player)) {   // a miss over the pit returns the player to the start
        if (pc->first()) pc->first()->fallResetY = -4.0f;
        if (pc->third()) pc->third()->fallResetY = -4.0f;
    }
}

std::string WithoutGuids(const std::string& t) {
    std::istringstream in(t); std::string line, out;
    while (std::getline(in, line)) if (line.find("guid \"") == std::string::npos) out += line + "\n";
    return out;
}

std::string Text() {
    Scene s("lab");
    BuildLab(s);
    return SceneSerializer::Serialize(s);
}

}  // namespace

int main() {
    RUN_SUITE("player_feel_lab");
    const std::string text = Text();
    CHECK(text.size() > 1000);
    if (const char* out = std::getenv("OKAY_WRITE_LAB")) {
        std::ofstream f(out, std::ios::binary);
        f << text;
        std::printf("wrote %s (%zu bytes)\n", out, text.size());
        return 0;
    }
#ifdef OKAY_SOURCE_DIR
    const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/player/player_feel_lab.okayscene";
    std::ifstream in(path, std::ios::binary);
    CHECK(in.good());
    std::stringstream ss; ss << in.rdbuf();
    CHECK(WithoutGuids(ss.str()) == WithoutGuids(text));

    Scene sc("play");
    sc.physicsEnabled = true;
    sc.UseFixedStepPhysics(true);
    CHECK(SceneSerializer::LoadFromFile(sc, path));
    GameObject* p = sc.Find("Player");
    CHECK(p != nullptr);
    GameObject* label = sc.Find("PresetLabel");
    CHECK(label != nullptr && label->GetComponent<TextRenderer>() != nullptr);
    if (p && label) {
        sc.Start();
        auto frames = [&](double sec, std::vector<char> k) {
            for (int i = 0, n = (int)std::lround(sec * 60.0); i < n; ++i) { Input::FeedKeys(k); sc.Update(1.0f / 60.0f); }
            Input::FeedKeys({});
        };
        auto* pc = PlayerController::Of(p);
        CHECK(pc != nullptr && pc->third() != nullptr);
        frames(1.0, {});
        CHECK(pc->third()->controllerPreset == 0);
        for (int pick = 0; pick < ControllerPresets::Count; ++pick) {
            frames(0.05, {(char)('1' + pick)}); frames(0.05, {});
            CHECK(pc->third()->controllerPreset == pick);
            CHECK(label->GetComponent<TextRenderer>()->text.find(ControllerPresets::Name(pick)) != std::string::npos);
        }
        // Run track: Classic running for 3 s from the start line covers the markers (z falls by 10 m or more).
        frames(0.05, {'1'}); frames(0.05, {});
        p->transform->SetPosition({-4.0f, 0.05f, 2.0f});
        frames(3.0, {'w', Input::KeyShift});
        CHECK(p->transform->Position().z < -8.0f);
        CHECK(p->transform->Position().y > -0.5f);
    }
#endif
    TEST_MAIN_RESULT();
}
