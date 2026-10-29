#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace okay;

// Logs player and camera world Y every frame through pickup, drop, V and a
// walk, at a low frame rate with an occasional hitch. The lens stays above the
// ground (top at y = 0) and the body does not fall through it.

static Camera* EyeCamera(GameObject* player) {
    if (!player || !player->transform) return nullptr;
    for (Transform* c : player->transform->Children())
        if (c && c->gameObject)
            if (auto* cam = c->gameObject->GetComponent<Camera>()) return cam;
    return nullptr;
}

static bool Finite3(const Vec3& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

static void Sample(const char* tag, int frame, GameObject* player, Camera* cam) {
    const Vec3 pp = player->transform->Position();
    const Vec3 cp = cam->transform->Position();
    std::cout << tag << " f=" << frame
              << " py=" << pp.y << " cy=" << cp.y
              << " px=" << pp.x << " pz=" << pp.z
              << " cx=" << cp.x << " cz=" << cp.z << "\n";
    CHECK(Finite3(pp));
    CHECK(Finite3(cp));
    CHECK(pp.y > -0.05f);
    CHECK(cp.y > 0.25f);
}

static void Play(const char* tag, Scene& sc, GameObject* player, Camera* cam,
                 float dt, int frames, const std::vector<char>& keys, int& frame) {
    Input::FeedKeys(keys);
    for (int i = 0; i < frames; ++i) {
        const float step = (i == frames / 2) ? 0.1f : dt;
        sc.Update(step);
        Sample(tag, frame++, player, cam);
    }
}

static void RunTemplate(void (*build)(Scene&), const char* tag) {
    Scene sc(tag);
    sc.physicsEnabled = true;
    build(sc);
    GameObject* player = sc.Find("Player");
    GameObject* crate = sc.Find("Carry Crate");
    CHECK(player && crate);
    if (!player || !crate) return;
    Camera* cam = EyeCamera(player);
    CHECK(cam);
    if (!cam) return;
    if (GameObject* block = sc.Find("Block"))
        block->transform->SetPosition({12.0f, 0.2f, 8.0f});

    sc.Start();
    int frame = 0;
    const float dt = 1.0f / 18.0f;
    Play(tag, sc, player, cam, dt, 20, {}, frame);

    player->transform->SetPosition({-0.95f, 1.0f, -1.10f});
    if (auto* rb = player->GetComponent<Rigidbody3D>()) {
        rb->velocity = Vec3::Zero;
        rb->prevPos = player->transform->Position();
        rb->hasPrevPos = true;
        rb->WakeUp();
    }
    Play(tag, sc, player, cam, dt, 24, {}, frame);

    Input::FeedKeys({'e'});
    sc.Update(dt);
    Sample(tag, frame++, player, cam);
    Input::FeedKeys({});
    Play(tag, sc, player, cam, dt, 8, {}, frame);
    auto* hold = player->GetComponent<ProceduralObjectHolding>();
    CHECK(hold && hold->Held() == crate);

    Play(tag, sc, player, cam, dt, 10, {'w'}, frame);

    Input::FeedKeys({'g'});
    sc.Update(0.1f);
    Sample(tag, frame++, player, cam);
    Input::FeedKeys({});
    Play(tag, sc, player, cam, dt, 6, {}, frame);
    CHECK(hold && hold->Held() == nullptr);

    for (int n = 0; n < 6; ++n) {
        Input::FeedKeys({'v'});
        sc.Update(n % 2 == 0 ? 0.1f : dt);
        Sample(tag, frame++, player, cam);
        Input::FeedKeys({});
        Play(tag, sc, player, cam, dt, 3, {}, frame);
    }

    // Back toward the spawn, still on the 40 m slab, including a hitch.
    player->transform->SetPosition({0.0f, 1.0f, 2.0f});
    if (auto* rb = player->GetComponent<Rigidbody3D>()) {
        rb->velocity = Vec3::Zero;
        rb->prevPos = player->transform->Position();
        rb->hasPrevPos = true;
        rb->WakeUp();
    }
    Play(tag, sc, player, cam, dt, 12, {}, frame);
    Play(tag, sc, player, cam, dt, 18, {'w'}, frame);

    // One step that starts the body inside the slab must come back out.
    player->transform->SetPosition({0.0f, -0.8f, 2.0f});
    if (auto* rb = player->GetComponent<Rigidbody3D>()) {
        rb->velocity = Vec3{0.0f, -5.0f, 0.0f};
        rb->WakeUp();
    }
    sc.Update(0.1f);
    Sample(tag, frame++, player, cam);
    CHECK(player->transform->Position().y > -0.05f);
}

int main() {
    RUN_SUITE("view ground");

    RunTemplate(&Templates::FPS, "fp");
    RunTemplate(&Templates::ThirdPerson, "tp");

    // Mouse degrees come from pixels, not from the frame time. A warp is
    // dropped. Sprint with a still mouse does not pitch.
    {
        auto boot = [](Scene& sc) {
            Templates::FPS(sc);
            sc.Start();
            Input::FeedMouse({400.0f, 300.0f}, 0);
            Input::FeedKeys({});
            sc.Update(1.0f / 60.0f);
            sc.Update(1.0f / 60.0f);
        };
        Scene a("look60");
        boot(a);
        auto* fpa = a.Find("Player")->GetComponent<FirstPersonController>();
        const float before = fpa->pitch;
        Input::FeedMouse({400.0f, 308.0f}, 0);
        a.Update(1.0f / 60.0f);
        const float d60 = fpa->pitch - before;

        Scene b("lookhitch");
        boot(b);
        auto* fpb = b.Find("Player")->GetComponent<FirstPersonController>();
        const float beforeB = fpb->pitch;
        Input::FeedMouse({400.0f, 308.0f}, 0);
        b.Update(0.1f);
        const float dHitch = fpb->pitch - beforeB;
        std::cout << "mouse 8px dt60=" << d60 << " dthitch=" << dHitch << "\n";
        CHECK_NEAR(d60, -1.2f, 0.02f);
        CHECK_NEAR(dHitch, d60, 0.02f);

        Scene c("warp");
        boot(c);
        auto* fpc = c.Find("Player")->GetComponent<FirstPersonController>();
        const float beforeC = fpc->pitch;
        Input::FeedMouse({400.0f, 4300.0f}, 0);
        c.Update(0.1f);
        std::cout << "warp pitch " << beforeC << " -> " << fpc->pitch << "\n";
        CHECK_NEAR(fpc->pitch, beforeC, 0.02f);
        CHECK(fpc->pitch <= 89.0f && fpc->pitch >= -89.0f);

        Scene d("sprint");
        boot(d);
        auto* fpd = d.Find("Player")->GetComponent<FirstPersonController>();
        const float beforeD = fpd->pitch;
        Input::FeedMouse({400.0f, 300.0f}, 0);
        Input::FeedKeys({'w', Input::KeyShift});
        d.Update(0.1f);
        d.Update(0.1f);
        std::cout << "sprint pitch " << beforeD << " -> " << fpd->pitch << "\n";
        CHECK_NEAR(fpd->pitch, beforeD, 0.02f);
        Input::FeedKeys({});
    }

    // TP to FP: a lens against the head is already hidden. The bow may miss
    // the skull; a frame that is inside it must not draw it.
    {
        Scene sc("hide");
        sc.physicsEnabled = true;
        Templates::ThirdPerson(sc);
        GameObject* player = sc.Find("Player");
        Camera* cam = EyeCamera(player);
        auto* pv = player->GetComponent<PlayerView>();
        sc.Start();
        Input::FeedKeys({});
        for (int i = 0; i < 5; ++i) sc.Update(1.0f / 60.0f);
        pv->SetView(0);
        bool sawClose = false;
        for (int i = 0; i < 40; ++i) {
            sc.Update(1.0f / 60.0f);
            Sample("hide", i, player, cam);
            Character* ch = player->GetComponent<Character>();
            GameObject* head = ch ? ch->Part(HumanoidMotion::B_HEAD) : nullptr;
            if (!head || !head->transform) continue;
            const float dist = (cam->transform->Position() - head->transform->Position()).Magnitude();
            bool inside = false;
            if (auto* mr = head->GetComponent<MeshRenderer>()) {
                Vec3 lo, hi;
                mr->mesh.Bounds(lo, hi);
                const Vec3 local = head->transform->InverseTransformPoint(cam->transform->Position());
                inside = local.x > lo.x && local.x < hi.x && local.y > lo.y && local.y < hi.y
                    && local.z > lo.z && local.z < hi.z;
            }
            if (inside || dist < 1.20f) {
                CHECK(cam->ignoreObject == player);
                sawClose = true;
            }
        }
        CHECK(sawClose);
        CHECK(!pv->Blending());
        Input::FeedKeys({});
    }

    TEST_MAIN_RESULT();
}
