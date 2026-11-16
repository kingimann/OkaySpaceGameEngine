#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include <cmath>
#include <string>

using namespace okay;

// The first-person arm looks like a normal first-person game (docs/try_it.md 7z).
//   Before: with nothing in your hands you saw no arm at all, a held item sat on a floating skin
//   cube with no forearm, a left click did not swing the arm, and there was no way to compare looks.
//   Now: Normal is the default for a new component (arm always in view, a forearm in the shirt
//   colour that leaves the bottom of the screen, click swings), plus presets.
namespace {
struct Rig {
    Scene scene{"FpNormal"};
    GameObject* player = nullptr;
    Character* ch = nullptr;
    FirstPersonHands* hands = nullptr;
    Camera* cam = nullptr;
    ProceduralObjectHolding* hold = nullptr;
};

void Build(Rig& r, bool oldLook) {
    r.scene.physicsEnabled = false;
    r.player = r.scene.CreateGameObject("Player");
    r.ch = r.player->AddComponent<Character>();
    r.ch->Apply();
    auto* fpc = r.player->AddComponent<FirstPersonController>();
    fpc->driveAnimation = false;
    r.hands = r.player->AddComponent<FirstPersonHands>();
    if (oldLook) r.hands->UseOldLook();
    r.hold = r.player->AddComponent<ProceduralObjectHolding>();
    GameObject* eye = r.scene.CreateGameObject("Eye");
    eye->transform->SetParent(r.player->transform, false);
    eye->transform->localPosition = {0, 1.6f, -0.2f};
    r.cam = eye->AddComponent<Camera>();
    r.cam->main = true;
    GameObject* sun = r.scene.CreateGameObject("Sun");
    sun->transform->localRotation = Quat::Euler(50.0f, -30.0f, 0.0f);
    sun->AddComponent<Light>();
    r.scene.Start();
    Input::FeedKeys({});
    for (int i = 0; i < 25; ++i) r.scene.Update(1.0f / 60.0f);
}

// Every pixel of the owner's picture that is not the clear colour, as a bounding box.
struct Box { int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1; int count = 0; };
Box OwnerPixels(Rig& r, int W, int H, Raster& out) {
    ApplySceneLight(r.scene);
    out.Resize(W, H);
    out.Clear(0xFF00FF00u);   // pure green: nothing else in the scene is this
    const float aspect = (float)W / H;
    r.cam->viewportAspect = aspect;
    const Mat4 vp = r.cam->ProjectionMatrix(aspect) * r.cam->ViewMatrix();
    RenderMeshes(out, r.scene, vp, r.cam->RenderPosition(), r.cam->ignoreObject);
    Box b;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (out.Get(x, y) != 0xFF00FF00u) {
                b.x0 = std::min(b.x0, x); b.x1 = std::max(b.x1, x);
                b.y0 = std::min(b.y0, y); b.y1 = std::max(b.y1, y);
                ++b.count;
            }
    return b;
}
}

int main() {
    RUN_SUITE("fp_hand_normal");

    // ---- presets ----
    {
        FirstPersonHands d;   // a new component is the Normal look
        CHECK(d.preset == FirstPersonHands::Preset::Normal);
        CHECK(d.showWhenEmpty);
        CHECK(d.swingOnClick);
        CHECK(d.right.forearm && d.right.matchShirt);
        CHECK(d.right.forearmLength > 0.9f);   // long enough to leave the bottom of the screen
        CHECK(d.left.enabled);                 // a two-handed item can still use it
        CHECK(d.holdMode == FirstPersonHands::HoldMode::Right);

        FirstPersonHands p;
        p.ApplyPreset(FirstPersonHands::Preset::BothArms);
        CHECK(p.holdMode == FirstPersonHands::HoldMode::Two && p.showWhenEmpty && p.left.enabled && p.right.enabled);
        p.ApplyPreset(FirstPersonHands::Preset::RightHandOnly);
        CHECK(p.right.enabled && !p.left.enabled && p.showWhenEmpty && p.right.forearm);
        CHECK(p.holdMode == FirstPersonHands::HoldMode::Right);
        p.ApplyPreset(FirstPersonHands::Preset::Minimal);
        CHECK(!p.right.forearm && !p.right.sleeve && p.showWhenEmpty && p.swingOnClick);
        p.ApplyPreset(FirstPersonHands::Preset::OldLook);
        CHECK(!p.showWhenEmpty && !p.swingOnClick && !p.right.forearm && !p.right.matchShirt);
        CHECK_NEAR(p.right.forearmLength, 0.42f, 1e-5f);   // the old numbers
        CHECK_NEAR(p.right.forearmThickness, 0.07f, 1e-5f);
        CHECK(p.preset == FirstPersonHands::Preset::OldLook);
        p.ApplyPreset(FirstPersonHands::Preset::Normal);
        CHECK(p.showWhenEmpty && p.right.forearm && p.right.matchShirt && p.swingOnClick);

        // A preset only changes the player view. What other players see is left alone.
        FirstPersonHands q;
        q.othersArmLift = 0.07f; q.othersRight.reach = 0.11f; q.othersItemOffset = {0.1f, 0.2f, 0.3f};
        q.ApplyPreset(FirstPersonHands::Preset::OldLook);
        CHECK_NEAR(q.othersArmLift, 0.07f, 1e-6f);
        CHECK_NEAR(q.othersRight.reach, 0.11f, 1e-6f);
        CHECK_NEAR(q.othersItemOffset.z, 0.3f, 1e-6f);
        for (int i = 0; i < FirstPersonHands::kPresetCount; ++i) {
            const auto pr = (FirstPersonHands::Preset)i;
            CHECK(std::string(FirstPersonHands::PresetName(pr)).size() > 3);
            CHECK(std::string(FirstPersonHands::PresetHelp(pr)).size() > 20);
        }
    }

    // ---- save and load ----
    {
        Scene a("Save");
        GameObject* g = a.CreateGameObject("Player");
        auto* h = g->AddComponent<FirstPersonHands>();
        h->ApplyPreset(FirstPersonHands::Preset::BothArms);
        h->right.matchShirt = false;
        h->attackButton = 1;
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("Load");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* h2 = b.Find("Player") ? b.Find("Player")->GetComponent<FirstPersonHands>() : nullptr;
        CHECK(h2 != nullptr);
        if (h2) {
            CHECK(h2->preset == FirstPersonHands::Preset::BothArms);
            CHECK(h2->holdMode == FirstPersonHands::HoldMode::Two);
            CHECK(!h2->right.matchShirt && h2->left.matchShirt);
            CHECK(h2->attackButton == 1 && h2->swingOnClick && h2->showWhenEmpty);
        }
        // A scene saved before presets has no "fphands3" line: it keeps its Old look.
        std::string old;
        {
            std::size_t pos = 0;
            while (pos < text.size()) {
                std::size_t e = text.find('\n', pos);
                if (e == std::string::npos) e = text.size();
                const std::string line = text.substr(pos, e - pos);
                if (line.find("fphands3") == std::string::npos) old += line + "\n";
                pos = e + 1;
            }
        }
        CHECK(old.find("fphands3") == std::string::npos);
        Scene c("Legacy");
        CHECK(SceneSerializer::Deserialize(c, old));
        auto* h3 = c.Find("Player") ? c.Find("Player")->GetComponent<FirstPersonHands>() : nullptr;
        CHECK(h3 != nullptr);
        if (h3) {
            CHECK(!h3->swingOnClick);
            CHECK(!h3->right.matchShirt && !h3->left.matchShirt);
        }
    }

    // ---- the owner sees an arm with nothing in hand, and it reaches the bottom of the screen ----
    {
        Rig n; Build(n, false);
        CHECK(n.ch->fpArmUp == 1.0f);
        CHECK(n.ch->OwnerViewArm() && n.ch->OwnerViewArm()->active);
        Raster r;
        const int W = 320, H = 240;
        Box b = OwnerPixels(n, W, H, r);
        CHECK(b.count > 400);
        CHECK(b.x0 > W / 3);                  // the lower right, not the middle of the view
        CHECK(b.y1 >= H - 2);                 // the arm leaves the bottom edge: it is connected, not floating
        CHECK(b.y0 > H / 3);                  // never fills the screen
        CHECK(b.count < W * H / 5);

        // Nothing crosses the near plane.
        float minAhead = 1e9f;
        auto walk = [&](auto&& self, GameObject* g) -> void {
            if (!g || !g->active || !g->transform) return;
            if (g->firstPersonOnly)
                if (auto* mr = g->GetComponent<MeshRenderer>())
                    for (const Vec3& v : mr->mesh.vertices) {
                        const Vec3 local = n.cam->transform->InverseTransformPoint(g->transform->TransformPoint(v));
                        minAhead = std::min(minAhead, -local.z);
                    }
            for (Transform* c : g->transform->Children()) if (c && c->gameObject) self(self, c->gameObject);
        };
        walk(walk, n.player);
        CHECK(minAhead >= n.cam->nearClip + 0.05f);

        // The sleeve colour is the character's shirt, the hand is the skin.
        GameObject* arm = n.ch->OwnerViewArm();
        bool sawShirt = false;
        if (arm && arm->transform)
            for (Transform* c : arm->transform->Children())
                if (c && c->gameObject)
                    if (auto* mr = c->gameObject->GetComponent<MeshRenderer>())
                        if (mr->enabled && !mr->mesh.vertices.empty()
                            && std::fabs(mr->color.r - n.ch->shirt.r) < 0.01f && std::fabs(mr->color.g - n.ch->shirt.g) < 0.01f)
                            sawShirt = true;
        CHECK(sawShirt);

        // Old look: nothing at all with empty hands. That was the complaint.
        Rig o; Build(o, true);
        CHECK(o.ch->fpArmUp == 0.0f);
        Raster r2;
        Box ob = OwnerPixels(o, W, H, r2);
        CHECK(ob.count == 0);
    }

    // ---- a left click swings the arm, and other players see the real arm ----
    {
        Rig n; Build(n, false);
        CHECK(!n.ch->Punching());
        Input::FeedMouse(Vec2{10, 10}, 1u);
        n.scene.Update(1.0f / 60.0f);
        Input::FeedMouse(Vec2{10, 10}, 0u);
        CHECK(n.ch->Punching());
        for (int i = 0; i < 60; ++i) n.scene.Update(1.0f / 60.0f);
        CHECK(!n.ch->Punching());

        Rig o; Build(o, true);
        Input::FeedMouse(Vec2{10, 10}, 1u);
        o.scene.Update(1.0f / 60.0f);
        Input::FeedMouse(Vec2{10, 10}, 0u);
        CHECK(!o.ch->Punching());
        Input::FeedMouse(Vec2{0, 0}, 0u);
    }

    // ---- others and the owner never see each other's copies ----
    {
        Rig n; Build(n, false);
        GameObject* arm = n.ch->OwnerViewArm();
        CHECK(arm != nullptr);
        auto flagged = [&](auto&& self, GameObject* g) -> void {
            if (!g) return;
            if (g->firstPersonOnly) CHECK(g->HiddenFromCamera(nullptr));   // another camera never draws the hand copy
            if (g->firstPersonOnly) CHECK(!g->HiddenFromCamera(n.cam->ignoreObject));   // the owner does
            if (g->transform) for (Transform* c : g->transform->Children()) if (c && c->gameObject) self(self, c->gameObject);
        };
        flagged(flagged, n.player);
        // Another camera sees the whole body: both real arms, the head, the legs.
        for (int bone : {2, 3, 4, 5, 6, 7, 8}) {
            GameObject* p = n.ch->Part(bone);
            CHECK(p && p->active && !p->firstPersonOnly && !p->HiddenFromCamera(nullptr));
        }
        // The real arms are not drawn a second time in the owner's own view.
        for (int bone : {3, 4, 5, 6, 7, 8}) {
            GameObject* p = n.ch->Part(bone);
            CHECK(p && p->HiddenFromCamera(n.cam->ignoreObject));
        }
        // The head and neck are hidden from the owner (no inside of the head).
        CHECK(n.ch->Part(2) && n.ch->Part(2)->HiddenFromCamera(n.cam->ignoreObject));
    }

    // ---- the first-person view of a held item sits on the hand and does not clip ----
    {
        Rig n; Build(n, false);
        GameObject* item = n.scene.CreateGameObject("Block");
        item->transform->localPosition = {0.3f, 1.0f, -0.7f};
        item->transform->localScale = {0.2f, 0.2f, 0.2f};
        item->AddComponent<MeshRenderer>()->mesh = Mesh::Cube(1.0f);
        item->AddComponent<HoldableObject>();
        n.scene.Start();
        CHECK(n.hold->Pickup(item));
        for (int i = 0; i < 60; ++i) n.scene.Update(1.0f / 60.0f);
        CHECK(n.hold->IsHolding());
        Raster r;
        Box b = OwnerPixels(n, 320, 240, r);
        CHECK(b.count > 400 && b.count < 320 * 240 / 4);
        CHECK(b.y1 >= 238);
        GameObject* copy = n.scene.Find("FP_OwnerItem");
        CHECK(copy && copy->active && copy->HiddenFromCamera(nullptr));
        // The real item is skipped by the owner's camera (no doubled item).
        CHECK(item->ownerCameraSkip != nullptr);
    }

    return 0;
}
