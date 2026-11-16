// Save games keep the runtime state of doors, movers, levers, animations and
// projectiles (issue #888, findings 62 and 63).
//  - 62: a logic door saved open used to take its open pose as the new "closed"
//    pose after a load, so it could never close again.
//  - 63: a one-shot Animator / SpriteAnimator played again from frame 0 after a
//    load, and a Projectile3D got its whole lifetime back.
// Each case: play, SaveGame::Save, SaveGame::LoadNow into a fresh scene, play on.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

namespace {

constexpr float kDt = 1.0f / 60.0f;

void Run(Scene& s, float seconds) {
    const int n = (int)std::lround(seconds / kDt);
    for (int i = 0; i < n; ++i) s.Update(kDt);
}

// Same rotation (q and -q are the same turn).
bool SameRot(const Quat& a, const Quat& b) {
    const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return d > 0.9999f;
}

GameObject* Obj(Scene& s, const char* name) {
    GameObject* g = s.Find(name);
    return (g && !g->IsDestroyed()) ? g : nullptr;
}

// Save `s` into `slot` and load it back into `out` (a fresh scene).
bool SaveAndLoad(Scene& s, const std::string& slot, Scene& out) {
    std::string err;
    if (!SaveGame::Save(s, slot, "test", &err)) { std::cerr << "  save failed: " << err << "\n"; return false; }
    if (!SaveGame::LoadNow(out, slot, &err)) { std::cerr << "  load failed: " << err << "\n"; return false; }
    return true;
}

void BuildDoorScene(Scene& s) {
    GameObject* door = s.CreateGameObject("Door");
    door->transform->SetPosition(Vec3{1.0f, 0.0f, 0.0f});
    auto* d = door->AddComponent<LogicTarget>();
    d->kind = LogicTarget::Kind::Door;
    d->openYaw = 90.0f;
    d->openOffset = Vec3{0.0f, 0.0f, 0.0f};
    d->moveTime = 0.2f;

    GameObject* lift = s.CreateGameObject("Lift");
    auto* m = lift->AddComponent<LogicTarget>();
    m->kind = LogicTarget::Kind::Mover;
    m->openOffset = Vec3{0.0f, 2.0f, 0.0f};
    m->moveTime = 1.0f;

    GameObject* lever = s.CreateGameObject("Lever");
    auto* l = lever->AddComponent<LogicSource>();
    l->kind = LogicSource::Kind::Button;
    l->toggle = true;
    l->pressKey = 0;
    l->pressPadButton = -1;
    s.Start();
}

void BuildAnimScene(Scene& s) {
    // A 4 s one-shot that moves x from 0 to 4.
    auto addClip = [&](const char* name) {
        GameObject* g = s.CreateGameObject(name);
        auto* a = g->AddComponent<Animator>();
        a->clip.loop = false;
        a->clip.smoothKeys = false;
        a->clip.AddKey("position.x", 0.0f, 0.0f);
        a->clip.AddKey("position.x", 4.0f, 4.0f);
    };
    addClip("OneShot");
    addClip("MidClip");

    // An 8-frame, 8 fps one-shot sprite clip (1 s long).
    auto addSprite = [&](const char* name) {
        GameObject* g = s.CreateGameObject(name);
        g->AddComponent<SpriteRenderer>();
        auto* sa = g->AddComponent<SpriteAnimator>();
        SpriteClip c;
        c.name = "boom";
        c.fps = 8.0f;
        c.loop = false;
        for (int i = 0; i < 8; ++i) c.frames.push_back("boom_" + std::to_string(i) + ".png");
        sa->clips.push_back(c);
    };
    addSprite("Boom");
    addSprite("BoomDone");

    GameObject* bullet = s.CreateGameObject("Bullet");
    auto* p = bullet->AddComponent<Projectile3D>();
    p->velocity = Vec3{0.0f, 0.0f, 1.0f};
    p->lifetime = 2.0f;
    p->damage = 0.0f;
    s.Start();
    s.Find("BoomDone")->GetComponent<SpriteAnimator>()->Play("boom");
}

} // namespace

int main() {
    RUN_SUITE("savegame_pose_anim");
    const fs::path dir = fs::temp_directory_path() / "okay_test_savegame_pose_anim";
    fs::remove_all(dir);
    fs::create_directories(dir);
    Save::SetBaseDir(dir.string());

    const Quat closedRot = Quat::Euler(0.0f, 0.0f, 0.0f);
    const Quat openRot = Quat::Euler(0.0f, 90.0f, 0.0f);

    // ---- Finding 62: a door saved open closes again to its authored pose ----
    {
        Scene a("Doors");
        BuildDoorScene(a);
        Run(a, 0.1f);
        auto* door = a.Find("Door")->GetComponent<LogicTarget>();
        auto* lift = a.Find("Lift")->GetComponent<LogicTarget>();
        auto* lever = a.Find("Lever")->GetComponent<LogicSource>();
        door->SetState(true);
        lift->SetState(true);
        lever->Press();
        Run(a, 1.0f);    // the door is fully open
        lift->SetState(false);   // stop the lift part-way back down
        Run(a, 0.3f);
        CHECK(SameRot(a.Find("Door")->transform->localRotation, openRot));
        CHECK(door->State());
        CHECK_NEAR(door->Amount(), 1.0f, 1e-4);
        const float liftT = lift->Amount();
        const float liftY = a.Find("Lift")->transform->localPosition.y;
        CHECK(liftT > 0.05f && liftT < 0.95f);
        lift->SetState(true);    // and running again when saved
        CHECK(lever->Output());

        Scene b("Doors");
        CHECK(SaveAndLoad(a, "doors", b));
        GameObject* d2 = Obj(b, "Door");
        GameObject* l2 = Obj(b, "Lift");
        GameObject* lv2 = Obj(b, "Lever");
        CHECK(d2 && l2 && lv2);
        if (d2 && l2 && lv2) {
            auto* door2 = d2->GetComponent<LogicTarget>();
            auto* lift2 = l2->GetComponent<LogicTarget>();
            auto* lever2 = lv2->GetComponent<LogicSource>();
            // Right after the load the door is open and knows it.
            CHECK(door2->State());
            CHECK_NEAR(door2->Amount(), 1.0f, 1e-4);
            CHECK_NEAR(lift2->Amount(), liftT, 1e-4);
            CHECK_NEAR(l2->transform->localPosition.y, liftY, 1e-3);
            Run(b, 1.0f);
            CHECK(SameRot(d2->transform->localRotation, openRot));   // still open, not turned a further 90
            CHECK(door2->State());
            CHECK(lever2->Output());                                 // a lever left on stays on
            // Close it: it ends at the authored closed pose.
            door2->SetState(false);
            Run(b, 1.0f);
            CHECK(SameRot(d2->transform->localRotation, closedRot));
            CHECK_NEAR(d2->transform->localPosition.x, 1.0f, 1e-4);
            CHECK_NEAR(door2->Amount(), 0.0f, 1e-4);
            // And it opens to the authored open pose again.
            door2->SetState(true);
            Run(b, 1.0f);
            CHECK(SameRot(d2->transform->localRotation, openRot));
            // The lift travels between its authored ends: stopping it brings it back to y = 0.
            lift2->SetState(false);
            Run(b, 2.0f);
            CHECK_NEAR(l2->transform->localPosition.y, 0.0f, 1e-3);
            lift2->SetState(true);
            float top = 0.0f;
            for (int i = 0; i < 90; ++i) { b.Update(kDt); top = std::max(top, l2->transform->localPosition.y); }
            CHECK_NEAR(top, 2.0f, 1e-3);   // the top is still 2 up
        }

        // Saving again while open (a second save of a loaded game) doesn't drift.
        Scene c("Doors");
        CHECK(SaveAndLoad(b, "doors2", c));
        if (GameObject* d3 = Obj(c, "Door")) {
            d3->GetComponent<LogicTarget>()->SetState(false);
            Run(c, 1.0f);
            CHECK(SameRot(d3->transform->localRotation, closedRot));
        } else {
            CHECK(false);
        }
    }

    // ---- Finding 63: animations and projectiles carry on ----
    {
        Scene a("Anims");
        BuildAnimScene(a);
        Run(a, 1.0f);
        a.Find("Boom")->GetComponent<SpriteAnimator>()->Play("boom");
        Run(a, 0.5f);
        // MidClip, Boom and Bullet are part-way; OneShot and BoomDone are at their end.
        auto* mid = a.Find("MidClip")->GetComponent<Animator>();
        auto* once = a.Find("OneShot")->GetComponent<Animator>();
        once->SetTime(4.5f);    // past its 4 s end: finished
        auto* boomDone = a.Find("BoomDone")->GetComponent<SpriteAnimator>();
        CHECK(!boomDone->playing);
        CHECK(boomDone->CurrentFrame() == 7);
        auto* boom = a.Find("Boom")->GetComponent<SpriteAnimator>();
        const int boomFrame = boom->CurrentFrame();
        CHECK(boom->playing);
        CHECK(boomFrame > 0 && boomFrame < 7);
        const float midTime = mid->Time();
        const float midX = a.Find("MidClip")->transform->localPosition.x;
        CHECK(midTime > 1.0f);
        const float bulletZ = a.Find("Bullet")->transform->Position().z;
        CHECK_NEAR(bulletZ, 1.5f, 0.05);

        Scene b("Anims");
        CHECK(SaveAndLoad(a, "anims", b));
        GameObject* m2 = Obj(b, "MidClip");
        GameObject* o2 = Obj(b, "OneShot");
        GameObject* bm2 = Obj(b, "Boom");
        GameObject* bd2 = Obj(b, "BoomDone");
        GameObject* bu2 = Obj(b, "Bullet");
        CHECK(m2 && o2 && bm2 && bd2 && bu2);
        if (m2 && o2 && bm2 && bd2 && bu2) {
            auto* mid2 = m2->GetComponent<Animator>();
            CHECK_NEAR(mid2->Time(), midTime, 1e-3);                // resumes where it was
            b.Update(kDt);
            CHECK_NEAR(mid2->Time(), midTime + kDt, 1e-3);
            CHECK(m2->transform->localPosition.x >= midX - 1e-3f);  // didn't snap back to 0
            CHECK(o2->GetComponent<Animator>()->Time() >= 4.0f);    // still finished
            CHECK_NEAR(o2->transform->localPosition.x, 4.0f, 1e-3);
            auto* bd = bd2->GetComponent<SpriteAnimator>();
            CHECK(!bd->playing);                                    // a finished one-shot stays finished
            CHECK(bd->CurrentFrame() == 7);
            CHECK(bd->CurrentClip() == "boom");
            CHECK(bd2->GetComponent<SpriteRenderer>()->texture == "boom_7.png");
            auto* bm = bm2->GetComponent<SpriteAnimator>();
            CHECK(bm->CurrentClip() == "boom");
            CHECK(bm->CurrentFrame() >= boomFrame);                 // mid-clip sprite resumes
            CHECK(bm->playing);
            // The bullet keeps flying from where it was and still dies at 2 s total.
            CHECK_NEAR(bu2->transform->Position().z, bulletZ + kDt, 0.02);
            Run(b, 0.25f);
            CHECK(Obj(b, "Bullet") != nullptr);
            if (GameObject* g = Obj(b, "Bullet")) CHECK_NEAR(g->transform->Position().z, bulletZ + 0.25f + kDt, 0.05);
            Run(b, 0.5f);   // 2.25 s of flight: past its lifetime
            b.FlushDestroyed();
            GameObject* gone = Obj(b, "Bullet");
            CHECK(gone == nullptr || !gone->GetComponent<Projectile3D>()->Alive());
            // The mid-clip sprite runs out and holds its last frame.
            Run(b, 1.0f);
            CHECK(!bm->playing);
            CHECK(bm->CurrentFrame() == 7);
        }
    }

    // ---- An old save (no runtime-state lines) still loads as before ----
    {
        Scene a("Old");
        BuildDoorScene(a);
        BuildAnimScene(a);
        Run(a, 0.5f);
        std::string err;
        CHECK(SaveGame::Save(a, "old", "old", &err));
        // Turn it into a version-1 file: no checksum, and no state lines but the ones
        // the first format had (body3 / body2 / script / graph / kv).
        const std::string path = SaveGame::SlotPath("old");
        std::ifstream in(path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        in.close();
        std::istringstream lines(ss.str());
        std::string line, out;
        bool first = true, inState = false;
        while (std::getline(lines, line)) {
            if (first) { out += "okaygame 1\n"; first = false; continue; }
            if (line == "--state--") inState = true;
            if (line == "--end--") break;
            if (inState && line != "--state--") {
                const std::string kind = line.substr(0, line.find('\t'));
                if (kind != "body3" && kind != "body2" && kind != "script" && kind != "graph" && kind != "kv") continue;
            }
            out += line + "\n";
        }
        std::ofstream o(path, std::ios::binary | std::ios::trunc);
        o << out;
        o.close();

        Scene b("Old");
        CHECK(SaveGame::LoadNow(b, "old", &err));
        GameObject* d2 = Obj(b, "Door");
        GameObject* m2 = Obj(b, "MidClip");
        GameObject* bu2 = Obj(b, "Bullet");
        CHECK(d2 && m2 && bu2);
        if (d2 && m2 && bu2) {
            // As before: the closed door works, the animation starts over, the bullet flies.
            CHECK_NEAR(m2->GetComponent<Animator>()->Time(), 0.0f, 1e-4);
            // The bullet's age starts over: alive 1.6 s after the load (2.1 s of flight in all).
            Run(b, 1.6f);
            CHECK(Obj(b, "Bullet") != nullptr);
            auto* door = d2->GetComponent<LogicTarget>();
            CHECK(!door->State());
            Run(b, 0.1f);
            door->SetState(true);
            Run(b, 1.0f);
            CHECK(SameRot(d2->transform->localRotation, openRot));
            door->SetState(false);
            Run(b, 1.0f);
            CHECK(SameRot(d2->transform->localRotation, closedRot));
        }
    }

    // ---- The shipped example (examples/savegame/save_mid_action.okayscene) ----
    {
        Scene a("Example");
        const std::string file = std::string(OKAY_EXAMPLES_DIR) + "/savegame/save_mid_action.okayscene";
        CHECK(SceneSerializer::LoadFromFile(a, file));
        a.Start();
        GameObject* door = Obj(a, "Door");
        GameObject* lever = Obj(a, "Lever");
        GameObject* slider = Obj(a, "Slider");
        CHECK(door && lever && slider && Obj(a, "Slow Bullet"));
        if (door && lever && slider) {
            Run(a, 0.2f);
            lever->GetComponent<LogicSource>()->Press();   // what O does (logic_press)
            Run(a, 1.5f);
            CHECK(door->GetComponent<LogicTarget>()->State());
            CHECK(SameRot(door->transform->localRotation, openRot));
            const float sliderT = slider->GetComponent<Animator>()->Time();
            Scene b("Example");
            CHECK(SaveAndLoad(a, "door_test", b));
            GameObject* d2 = Obj(b, "Door");
            GameObject* l2 = Obj(b, "Lever");
            GameObject* s2 = Obj(b, "Slider");
            CHECK(d2 && l2 && s2);
            if (d2 && l2 && s2) {
                CHECK_NEAR(s2->GetComponent<Animator>()->Time(), sliderT, 1e-3);
                Run(b, 0.2f);
                CHECK(SameRot(d2->transform->localRotation, openRot));
                l2->GetComponent<LogicSource>()->Press();   // pull the lever again: the door shuts
                Run(b, 1.5f);
                CHECK(!d2->GetComponent<LogicTarget>()->State());
                CHECK(SameRot(d2->transform->localRotation, closedRot));
                CHECK_NEAR(d2->transform->localPosition.x, -1.0f, 1e-4);
            }
        }
    }

    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
