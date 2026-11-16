// Headless pictures of the first-person hand. Not a test: a tool.
//
// Usage: fp_hand_shot <outdir> [old|normal]
//
// Builds the default First Person starter player (the same one the editor's
// First Person template makes), then for each situation (idle, walk, run,
// swing, hold, look up, look down, crouch) renders three pictures with the
// software renderer and puts them side by side in one PNG:
//   1. the owner's own camera (what the player sees),
//   2. a second camera in front of the player (what other players see),
//   3. a second camera at the player's side.
// The owner's camera skips the player's body exactly as the game does
// (Camera::ignoreObject); the second camera skips nothing.
// With "old" the FirstPersonHands are put to the old look preset, "normal" to
// the Normal preset (the default for a new component).
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace okay;

namespace {
constexpr int kW = 360, kH = 300;

void BuildWorld(Scene& s) {
    GameObject* ground = s.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -0.5f, 0};
    ground->transform->localScale = {40, 1, 40};
    ground->AddComponent<MeshRenderer>()->mesh = Mesh::Cube(1.0f);
    ground->GetComponent<MeshRenderer>()->color = Color::FromBytes(96, 130, 92);
    ground->AddComponent<BoxCollider3D>();
    ground->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    auto box = [&](const char* n, Vec3 p, Vec3 sc, Color c) {
        GameObject* g = s.CreateGameObject(n);
        g->transform->localPosition = p;
        g->transform->localScale = sc;
        auto* mr = g->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube(1.0f);
        mr->color = c;
        return g;
    };
    box("Wall", {0, 1.5f, -6.0f}, {8, 3, 0.4f}, Color::FromBytes(170, 160, 150));
    box("PostL", {-2.5f, 0.5f, -3.5f}, {0.3f, 1, 0.3f}, Color::FromBytes(180, 90, 80));
    box("PostR", {2.5f, 0.5f, -3.5f}, {0.3f, 1, 0.3f}, Color::FromBytes(80, 110, 190));
    GameObject* sun = s.CreateGameObject("Sun");
    sun->transform->localRotation = Quat::Euler(50.0f, -30.0f, 0.0f);
    sun->AddComponent<Light>();
}

void Blit(std::vector<std::uint8_t>& out, int outW, int ox, int oy, const Raster& r) {
    for (int y = 0; y < r.height; ++y)
        for (int x = 0; x < r.width; ++x) {
            std::uint32_t v = r.Get(x, y);
            std::size_t i = ((std::size_t)(oy + y) * outW + (ox + x)) * 4;
            out[i + 0] = v & 0xFF; out[i + 1] = (v >> 8) & 0xFF; out[i + 2] = (v >> 16) & 0xFF; out[i + 3] = 255;
        }
}

void Render(Raster& r, Scene& sc, const Mat4& vp, const Vec3& eye, const GameObject* ignore) {
    r.Resize(kW, kH);
    r.Clear(0xFFD8B890u);
    ApplySceneLight(sc);
    RenderMeshes(r, sc, vp, eye, ignore);
}

struct Case { const char* name; };
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: fp_hand_shot <outdir> [old|normal]\n"); return 2; }
    const std::string outdir = argv[1];
    const std::string mode = argc > 2 ? argv[2] : "normal";
    const char* cases[] = {"idle", "walk", "run", "swing", "hold", "holdwalk", "lookup", "lookdown", "crouch"};
    for (const char* cs : cases) {
        Scene sc("FpShot");
        BuildWorld(sc);
        GameObject* player = Templates::AddFirstPersonPlayer(sc, {0, 0.05f, 0});
        Templates::AddTemplateHoldables(sc, player);
#ifdef FPSHOT_BEFORE
        // Before the change there are no presets: the component is the old look.
#else
        if (mode == "old") {
            if (auto* h = player->GetComponent<FirstPersonHands>()) h->UseOldLook();
        } else if (mode == "normal") {
            if (auto* h = player->GetComponent<FirstPersonHands>()) h->UseNormalArms();
        }
#endif
        auto* fpc = player->GetComponent<FirstPersonController>();
        auto* hold = player->GetComponent<ProceduralObjectHolding>();
        const std::string c = cs;
        const bool wantHold = c == "hold" || c == "holdwalk";
        Input::FeedKeys({});
        for (const auto& o : sc.Objects())
            if (auto* cm = o->GetComponent<Camera>()) if (o->IsSelfOrDescendantOf(player)) cm->viewportAspect = (float)kW / kH;
        sc.Start();
        for (int i = 0; i < 20; ++i) sc.Update(1.0f / 60.0f);
        if (wantHold && hold) {
            if (GameObject* b = sc.Find("Block")) { b->transform->localPosition = {0.4f, 0.3f, -0.9f}; hold->Pickup(b); }
            for (int i = 0; i < 40; ++i) sc.Update(1.0f / 60.0f);
        }
        std::vector<char> keys;
        if (c == "walk" || c == "holdwalk") keys = {'w'};
        if (c == "run") keys = {'w', Input::KeyShift};
        if (c == "crouch") keys = {Input::KeyCtrl};
        Input::FeedKeys(keys);
        int frames = (c == "walk" || c == "holdwalk") ? 34 : c == "run" ? 22 : 40;
        if (fpc && c == "lookup") fpc->pitch = 50.0f;
        if (fpc && c == "lookdown") fpc->pitch = -60.0f;
        for (int i = 0; i < frames; ++i) {
            sc.Update(1.0f / 60.0f);
            if (c == "swing" && i == 32) {
                if (auto* ch = player->GetComponent<Character>()) ch->Punch();
            }
        }
        if (c == "swing") for (int i = 0; i < 3; ++i) sc.Update(1.0f / 60.0f);
        // Camera pass (the owner).
        Camera* own = nullptr;
        for (const auto& o : sc.Objects())
            if (auto* cm = o->GetComponent<Camera>()) if (cm->enabled && o->IsSelfOrDescendantOf(player)) own = cm;
        std::vector<std::uint8_t> img((std::size_t)kW * 3 * kH * 4, 0);
        Raster r;
        if (own) {
            const float aspect = (float)kW / kH;
            Mat4 vp = own->ProjectionMatrix(aspect) * own->ViewMatrix();
            Render(r, sc, vp, own->RenderPosition(), own->ignoreObject);
            Blit(img, kW * 3, 0, 0, r);
        }
        const Vec3 pp = player->transform->Position();
        const float aspect = (float)kW / kH;
        const Mat4 proj = Mat4::Perspective(45.0f, aspect, 0.1f, 100.0f);
        Vec3 fe = pp + Vec3{0.0f, 1.25f, -3.2f};
        Render(r, sc, proj * Mat4::LookAt(fe, pp + Vec3{0, 0.95f, 0}, Vec3{0, 1, 0}), fe, nullptr);
        Blit(img, kW * 3, kW, 0, r);
        Vec3 se = pp + Vec3{3.2f, 1.25f, 0.0f};
        Render(r, sc, proj * Mat4::LookAt(se, pp + Vec3{0, 0.95f, 0}, Vec3{0, 1, 0}), se, nullptr);
        Blit(img, kW * 3, kW * 2, 0, r);
        Image im; im.Set(kW * 3, kH, img);
        const std::string path = outdir + "/" + mode + "_" + c + ".png";
        im.SavePNG(path);
        std::printf("wrote %s (owner camera %s)\n", path.c_str(), own ? "found" : "MISSING");
        Input::FeedKeys({});
    }
    return 0;
}
