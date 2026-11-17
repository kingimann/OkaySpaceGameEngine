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

// examples/fp_hand/fp_hand_test.okayscene: the First Person starter's player, a small mirror
// (a second camera that looks back at you), things to pick up, and text saying what to look for.
// Written with the scene writer: fp_hand_shot --scene <out.okayscene>
int WriteExampleScene(const char* path) {
    Scene sc("FP Hand Test");
    BuildWorld(sc);
    GameObject* player = Templates::AddFirstPersonPlayer(sc, {0, 0.05f, 0});
    Templates::AddTemplateHoldables(sc, player);
    auto prop = [&](const char* name, Vec3 pos, Vec3 scale, Color col, bool two) {
        GameObject* g = sc.CreateGameObject(name);
        g->transform->localPosition = pos;
        g->transform->localScale = scale;
        auto* mr = g->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube(1.0f);
        mr->color = col;
        g->AddComponent<BoxCollider3D>();
        g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Dynamic;
        auto* ho = g->AddComponent<HoldableObject>();
        if (two) { ho->ApplyPreset(HoldableObject::Preset::CarryFront); ho->weight = 3.0f; }
        else { ho->ApplyPreset(HoldableObject::Preset::OneHandRight); ho->weight = 0.3f; }
        auto* pk = g->AddComponent<ItemPickup>();
        pk->item = name; pk->holdInHands = true; pk->interactKey = 'e';
        pk->pickupOnTouch = false; pk->autoRadius = 0.0f; pk->interactRange = 3.0f;
    };
    prop("Tiny Cube", {-0.5f, 0.1f, -2.4f}, {0.12f, 0.12f, 0.12f}, Color::FromBytes(220, 80, 80), false);
    prop("Medium Block", {1.6f, 0.2f, -2.2f}, {0.4f, 0.4f, 0.4f}, Color::FromBytes(90, 150, 220), false);
    prop("Big Box", {-1.8f, 0.35f, -3.4f}, {0.7f, 0.7f, 0.7f}, Color::FromBytes(150, 200, 110), true);
    // The mirror: a second camera that rides on the player, a few metres in front, looking back.
    GameObject* mir = sc.CreateGameObject("Mirror Camera");
    mir->transform->SetParent(player->transform, false);
    mir->transform->localPosition = {0.0f, 1.3f, -3.2f};
    mir->transform->localRotation = Quat::Euler(0.0f, 180.0f, 0.0f);
    auto* mc = mir->AddComponent<Camera>();
    mc->main = false;
    mc->depth = 5.0f;
    mc->fieldOfView = 40.0f;
    mc->rectX = 0.68f; mc->rectY = 0.04f; mc->rectW = 0.30f; mc->rectH = 0.34f;
    mc->clearFlags = Camera::ClearFlags::Skybox;
    GameObject* help = sc.CreateGameObject("What to look for");
    auto* t = help->AddComponent<TextRenderer>();
    t->text = "FIRST PERSON ARM TEST. Your arm is at the lower right with a forearm in your shirt colour. Left click swings it. "
              "Press E near a block to hold it. The small picture (top right) is how OTHER players see you: a whole body, no floating hand. "
              "Select the Player > First Person Hands > Preset to compare looks.";
    t->screenSpace = true; t->anchor = UIAnchor::StretchHorizontal;
    t->screenPos = {12, 48}; t->size = {330, 120}; t->vcenter = false; t->wrap = true;
    t->pixelSize = 2.0f; t->outline = true;
    return SceneSerializer::SaveToFile(sc, path) ? 0 : 1;
}

// examples/fp_hand/fp_hands_motion.okayscene: a small gym for the first-person hand MOTION (breathing, bob, sprint,
// crouch, jump and land, strafe, aim, wall pull-back) and the held-item grips (pistol, rifle, flashlight, crate).
// Written with the scene writer: fp_hand_shot --motion-scene <out.okayscene>
static GameObject* MotionProp(Scene& sc, const char* name, Vec3 pos, Vec3 scale, Color col) {
    GameObject* g = sc.CreateGameObject(name);
    g->transform->localPosition = pos;
    g->transform->localScale = scale;
    auto* mr = g->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube(1.0f);
    mr->color = col;
    g->AddComponent<BoxCollider3D>();
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Dynamic;
    g->AddComponent<HoldableObject>()->weight = 0.8f;   // style stays Automatic: the name picks the grip
    auto* pk = g->AddComponent<ItemPickup>();
    pk->item = name; pk->holdInHands = true; pk->interactKey = 'e';
    pk->pickupOnTouch = false; pk->autoRadius = 0.0f; pk->interactRange = 3.0f;
    return g;
}
static void BuildMotionGym(Scene& sc) {
    BuildWorld(sc);
    auto block = [&](const char* n, Vec3 p, Vec3 sc3, Color c, bool solid) {
        GameObject* g = sc.CreateGameObject(n);
        g->transform->localPosition = p; g->transform->localScale = sc3;
        auto* mr = g->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube(1.0f); mr->color = c;
        if (solid) { g->AddComponent<BoxCollider3D>(); g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static; }
    };
    block("Wall To Walk Into", {0, 1.2f, -1.9f}, {2.4f, 2.4f, 0.3f}, Color::FromBytes(200, 190, 170), true);
    block("Step", {3.0f, 0.3f, -2.0f}, {1.5f, 0.6f, 1.5f}, Color::FromBytes(150, 120, 90), true);
    block("Low Beam", {-3.5f, 1.35f, -1.5f}, {1.2f, 0.25f, 3.0f}, Color::FromBytes(110, 110, 130), true);
}
int WriteMotionScene(const char* path) {
    Scene sc("FP Hands Motion");
    BuildMotionGym(sc);
    GameObject* player = Templates::AddFirstPersonPlayer(sc, {0, 0.05f, 2.0f});
    Templates::AddTemplateHoldables(sc, player);
    // The generic template blocks stay out of the way; the gym has its own items.
    MotionProp(sc, "Pistol", {-0.6f, 0.1f, 0.6f}, {0.08f, 0.14f, 0.28f}, Color::FromBytes(60, 60, 70));
    MotionProp(sc, "Rifle", {0.6f, 0.1f, 0.6f}, {0.09f, 0.12f, 0.9f}, Color::FromBytes(80, 70, 55));
    MotionProp(sc, "Flashlight", {-1.6f, 0.1f, 0.9f}, {0.07f, 0.07f, 0.32f}, Color::FromBytes(210, 190, 70));
    MotionProp(sc, "Crate", {1.7f, 0.2f, 1.0f}, {0.45f, 0.4f, 0.45f}, Color::FromBytes(160, 120, 70));
    GameObject* mir = sc.CreateGameObject("Mirror Camera");
    mir->transform->SetParent(player->transform, false);
    mir->transform->localPosition = {0.0f, 1.3f, -3.2f};
    mir->transform->localRotation = Quat::Euler(0.0f, 180.0f, 0.0f);
    auto* mc = mir->AddComponent<Camera>();
    mc->main = false; mc->depth = 5.0f; mc->fieldOfView = 40.0f;
    mc->rectX = 0.68f; mc->rectY = 0.04f; mc->rectW = 0.30f; mc->rectH = 0.34f;
    mc->clearFlags = Camera::ClearFlags::Skybox;
    GameObject* help = sc.CreateGameObject("What to look for");
    auto* t = help->AddComponent<TextRenderer>();
    t->text = "FIRST PERSON HANDS: MOTION. Stand still: the hand breathes. W: it bobs with your steps. Shift: sprint pose. Ctrl: it lowers. "
              "Space: kick on take-off and landing. A / D: it tilts. Walk into the wall ahead: the hand pulls back and down. "
              "E picks up Pistol, Rifle, Flashlight, Crate (each is held differently). With a gun: click fires, right mouse aims, R reloads. "
              "Settings > Accessibility > Reduce motion turns the sway, bob and tilt off.";
    t->screenSpace = true; t->anchor = UIAnchor::StretchHorizontal;
    t->screenPos = {12, 48}; t->size = {360, 170}; t->vcenter = false; t->wrap = true;
    t->pixelSize = 2.0f; t->outline = true;
    return SceneSerializer::SaveToFile(sc, path) ? 0 : 1;
}

// Strips of the owner's own camera for the motion states. Headless software renders.
// fp_hand_shot --strips <outdir>
int MotionStrips(const std::string& outdir) {
    struct Shot { const char* name; };
    auto build = [&](Scene& sc, GameObject*& player) {
        BuildMotionGym(sc);
        player = Templates::AddFirstPersonPlayer(sc, {0, 0.05f, 2.0f});
        Templates::AddTemplateHoldables(sc, player);
        MotionProp(sc, "Pistol", {-0.4f, 0.3f, 1.3f}, {0.08f, 0.14f, 0.28f}, Color::FromBytes(60, 60, 70));
        MotionProp(sc, "Rifle", {0.4f, 0.3f, 1.3f}, {0.09f, 0.12f, 0.9f}, Color::FromBytes(80, 70, 55));
        MotionProp(sc, "Flashlight", {-0.9f, 0.3f, 1.3f}, {0.07f, 0.07f, 0.32f}, Color::FromBytes(210, 190, 70));
        MotionProp(sc, "Crate", {0.9f, 0.3f, 1.3f}, {0.45f, 0.4f, 0.45f}, Color::FromBytes(160, 120, 70));
        for (const auto& o : sc.Objects())
            if (auto* cm = o->GetComponent<Camera>()) if (o->IsSelfOrDescendantOf(player)) cm->viewportAspect = (float)kW / kH;
        sc.Start();
        Input::FeedKeys({});
        for (int i = 0; i < 40; ++i) sc.Update(1.0f / 60.0f);
    };
    struct Row { const char* file; std::vector<const char*> cells; };
    const std::vector<Row> rows = {
        {"states", {"idle", "walk", "sprint", "crouch", "air", "land", "strafe", "wall"}},
        {"grips", {"empty", "flashlight", "crate", "pistol", "rifle", "rifle_aim"}},
        {"events", {"swing", "use", "fire", "reload", "equip"}},
    };
    for (const Row& row : rows) {
        const int n = (int)row.cells.size();
        std::vector<std::uint8_t> img((std::size_t)kW * n * kH * 4, 0);
        int col = 0;
        for (const char* cell : row.cells) {
            Scene sc("FpStrip");
            GameObject* player = nullptr;
            build(sc, player);
            const std::string c = cell;
            auto* ch = player->GetComponent<Character>();
            auto* hold = player->GetComponent<ProceduralObjectHolding>();
            auto* hands = player->GetComponent<FirstPersonHands>();
            auto step = [&](int frames, std::vector<char> keys = {}) { for (int i = 0; i < frames; ++i) { Input::FeedKeys(keys); sc.Update(1.0f / 60.0f); } };
            auto grab = [&](const char* name) { if (GameObject* g = sc.Find(name)) hold->Pickup(g); step(60); };
            if (c == "sprint") hands->showWhileRunning = true;
            if (c == "flashlight") grab("Flashlight");
            if (c == "crate") grab("Crate");
            if (c == "pistol" || c == "fire" || c == "reload" || c == "equip" || c == "use") grab("Pistol");
            if (c == "rifle" || c == "rifle_aim") grab("Rifle");
            if (c == "idle" || c == "empty") step(60);
            if (c == "walk") step(37, {'w'});
            if (c == "sprint") step(40, {'w', Input::KeyShift});
            if (c == "crouch") step(45, {Input::KeyCtrl});
            if (c == "air") { step(20, {'w'}); step(6, {' '}); step(4, {'w'}); }
            if (c == "land") { step(20, {'w'}); step(6, {' '}); step(75, {'w'}); }
            if (c == "strafe") step(40, {'d'});
            if (c == "wall") { player->transform->localPosition = {0, 0.05f, -0.75f}; step(10); step(60, {}); }
            if (c == "rifle_aim") { ch->SetFpAim(true); step(40); }
            if (c == "swing") { ch->TriggerFpEvent("swing"); step(9); }
            if (c == "use") { ch->TriggerFpEvent("use"); step(8); }
            if (c == "fire") { ch->TriggerFpEvent("fire"); step(4); }
            if (c == "reload") { ch->TriggerFpEvent("reload"); step(50); }
            if (c == "equip") { ch->TriggerFpEvent("equip"); step(6); }
            Camera* own = nullptr;
            for (const auto& o : sc.Objects())
                if (auto* cm = o->GetComponent<Camera>()) if (cm->enabled && o->IsSelfOrDescendantOf(player)) own = cm;
            Raster r;
            if (own) {
                const float aspect = (float)kW / kH;
                Render(r, sc, own->ProjectionMatrix(aspect) * own->ViewMatrix(), own->RenderPosition(), own->ignoreObject);
                Blit(img, kW * n, kW * col, 0, r);
            }
            ++col;
        }
        Image im; im.Set(kW * n, kH, img);
        im.SavePNG(outdir + "/" + row.file + ".png");
        std::printf("wrote %s/%s.png (%d cells: ", outdir.c_str(), row.file, n);
        for (const char* cell : row.cells) std::printf("%s ", cell);
        std::printf(")\n");
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--motion-scene") return WriteMotionScene(argv[2]);
    if (argc >= 3 && std::string(argv[1]) == "--strips") return MotionStrips(argv[2]);
    if (argc >= 3 && std::string(argv[1]) == "--scene") return WriteExampleScene(argv[2]);
    if (argc < 2) { std::fprintf(stderr, "usage: fp_hand_shot <outdir> [old|normal]\n"); return 2; }
    const std::string outdir = argv[1];
    const std::string mode = argc > 2 ? argv[2] : "normal";
    std::vector<const char*> cases = {"idle", "walk", "run", "swing", "hold", "holdwalk", "lookup", "lookdown", "crouch"};
    if (mode == "both" || mode == "right" || mode == "minimal") cases = {"idle", "hold"};
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
        } else if (mode == "both" || mode == "right" || mode == "minimal") {
            if (auto* h = player->GetComponent<FirstPersonHands>())
                h->ApplyPreset(mode == "both" ? FirstPersonHands::Preset::BothArms
                             : mode == "right" ? FirstPersonHands::Preset::RightHandOnly
                                               : FirstPersonHands::Preset::Minimal);
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
