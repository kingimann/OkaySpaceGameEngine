// OkaySurvival — a small standalone survival game built on the OkaySpace engine.
//
// This program assembles the game scene with the engine API and writes the two files
// a shipped OkaySpace game needs beside the player runtime: game.okayscene (the
// world) and game.okayconfig (window + startup). Run it as:
//     okaysurvival_gen <output_dir>
// then drop the player runtime (renamed) + SDL2 next to the output. The result is a
// double-click survival game running entirely on the engine — no editor.
//
// The loop: your third-person character's hunger/thirst/stamina drain over a
// day/night cycle. Eat at the berry bush, drink at the well, warm up at the campfire,
// don't drown in the lake or linger in the toxic pit. Survive.
#include <Okay.hpp>
#include <cstdio>
#include <cmath>
#include <string>
using namespace okay;

static void Tree(Scene& s, float x, float z) {
    GameObject* trunk = s.CreateGameObject("Tree");
    trunk->transform->localPosition = {x, 1.0f, z};
    trunk->transform->localScale = {0.5f, 2.0f, 0.5f};
    auto* mr = trunk->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cylinder(); mr->color = Color::FromBytes(110, 80, 55);
    trunk->AddComponent<BoxCollider3D>();
    trunk->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;

    GameObject* crown = s.CreateGameObject("Leaves");
    crown->transform->localPosition = {x, 2.6f, z};
    crown->transform->localScale = {1.8f, 1.8f, 1.8f};
    auto* cm = crown->AddComponent<MeshRenderer>();
    cm->mesh = Mesh::Sphere(); cm->color = Color::FromBytes(60, 130, 60);
}

static void Rock(Scene& s, float x, float z, float r) {
    GameObject* rock = s.CreateGameObject("Rock");
    rock->transform->localPosition = {x, r * 0.5f, z};
    rock->transform->localScale = {r, r, r};
    auto* mr = rock->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Sphere(); mr->color = Color::FromBytes(120, 120, 128);
    rock->AddComponent<SphereCollider3D>();
    rock->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
}

// A flat "pad" with a trigger SurvivalZone — the player gets the effect while standing on it.
static void Zone(Scene& s, const char* name, float x, float z, float size,
                 Color color, SurvivalZone::Effect effect, float amount) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = {x, 0.05f, z};
    g->transform->localScale = {size, 0.1f, size};
    auto* mr = g->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube(); mr->color = color;
    mr->emissive = Color{color.r * 0.3f, color.g * 0.3f, color.b * 0.3f, 1.0f};
    auto* col = g->AddComponent<BoxCollider3D>();
    col->size = {1, 8, 1};            // tall trigger so the player always overlaps it
    col->isTrigger = true;
    auto* sz = g->AddComponent<SurvivalZone>();
    sz->effect = (int)effect; sz->amount = amount;
}

// A wolf: a dark capsule that wanders and chases/bites the player.
static void Wolf(Scene& s, float x, float z) {
    GameObject* w = s.CreateGameObject("Wolf");
    w->transform->localPosition = {x, 0.6f, z};
    auto* mr = w->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Capsule(); mr->color = Color::FromBytes(70, 70, 80);
    w->AddComponent<Rigidbody3D>();
    auto* col = w->AddComponent<CapsuleCollider3D>();
    col->radius = 0.4f; col->height = 1.2f;
    auto* n = w->AddComponent<NPCController>();
    n->behavior = (int)NPCController::Behavior::Chase;
    n->moveSpeed = 3.2f; n->sightRange = 12.0f; n->wanderRadius = 8.0f;
    n->attackRange = 1.6f; n->attackDamage = 7.0f; n->attackInterval = 1.2f;
}

static void Bar(Scene& s, const char* name, float y, Color fill) {
    GameObject* g = s.CreateGameObject(name);
    auto* pb = g->AddComponent<UIProgressBar>();
    pb->position = {20.0f, y};
    pb->size = {220.0f, 22.0f};
    pb->anchor = UIAnchor::TopLeft;
    pb->fill = fill; pb->fillEnd = fill;
    pb->showPercent = true;
}

int main(int argc, char** argv) {
    std::string outDir = argc > 1 ? argv[1] : ".";
    if (!outDir.empty() && outDir.back() != '/' && outDir.back() != '\\') outDir += '/';

    Scene scene("OkaySurvival");

    // ---- Sun + day/night cycle ----
    GameObject* sun = scene.CreateGameObject("Sun");
    auto* light = sun->AddComponent<Light>();
    light->type = Light::Type::Directional;
    sun->transform->localRotation = Quat::Euler({50, -30, 0});
    auto* dn = sun->AddComponent<DayNightCycle>();
    dn->dayLengthSeconds = 180.0f;   // a day every 3 minutes
    dn->time = 8.0f;                  // start at morning

    // ---- Ground ----
    GameObject* ground = scene.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -0.5f, 0};
    ground->transform->localScale = {80, 1, 80};
    auto* gmr = ground->AddComponent<MeshRenderer>();
    gmr->mesh = Mesh::Cube(); gmr->color = Color::FromBytes(86, 120, 74);
    ground->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    ground->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;

    // ---- Scenery: a ring of trees + scattered rocks ----
    for (int i = 0; i < 12; ++i) {
        float a = (float)i / 12.0f * 6.2831853f;
        Tree(scene, std::cos(a) * 22.0f, std::sin(a) * 22.0f);
    }
    Tree(scene,  10,  -6); Tree(scene, -12, 8); Tree(scene, 6, 14); Tree(scene, -8, -14);
    Rock(scene,  5,  5, 1.2f); Rock(scene, -6, 3, 1.6f); Rock(scene, 9, -10, 1.0f);
    Rock(scene, -14, -4, 2.0f); Rock(scene, 14, 9, 1.4f);

    // ---- Survival resource zones (stand on them) ----
    Zone(scene, "Berry Bush", -7.0f,  -7.0f, 2.2f, Color::FromBytes( 80, 160,  70), SurvivalZone::Effect::Eat,   35.0f);
    Zone(scene, "Well",        8.0f,   7.0f, 2.0f, Color::FromBytes( 70, 140, 210), SurvivalZone::Effect::Drink, 35.0f);
    Zone(scene, "Campfire",    0.0f,  -3.0f, 1.6f, Color::FromBytes(230, 140,  60), SurvivalZone::Effect::Fire,   0.0f);
    Zone(scene, "Lake",       16.0f, -14.0f, 5.0f, Color::FromBytes( 50, 110, 190), SurvivalZone::Effect::Water,  0.0f);
    Zone(scene, "Toxic Pit", -16.0f,  12.0f, 3.0f, Color::FromBytes(120,  90, 160), SurvivalZone::Effect::Poison,30.0f);

    // ---- Player: blocky character, third-person ----
    GameObject* player = scene.CreateGameObject("Player");
    player->transform->localPosition = {0, 1.0f, 4};
    player->AddComponent<Character>()->Apply();
    player->AddComponent<Rigidbody3D>();
    {
        auto* col = player->AddComponent<BoxCollider3D>();
        col->size = {0.6f, 1.8f, 0.6f};
        col->offset = {0.0f, 0.9f, 0.0f};
    }
    player->AddComponent<ThirdPersonController>();

    // Survival stats + afflictions + persistence.
    auto* sv = player->AddComponent<SurvivalStats>();
    sv->hungerDrain = 1.2f; sv->thirstDrain = 1.6f; sv->regenWhenFed = 1.0f;
    player->AddComponent<PoisonStat>();      // the toxic pit poisons via this
    player->AddComponent<WetnessStat>()->chillPerSecond = 4.0f;
    auto* save = player->AddComponent<SurvivalSave>();
    save->saveKey = "okaysurvival"; save->loadOnStart = true;

    // Inventory + crafting: start with cloth, craft a bandage, use it to heal.
    auto* inv = player->AddComponent<Inventory>();
    inv->Add("cloth", 4);
    auto* craft = player->AddComponent<Crafting>();
    craft->AddRecipe("bandage", 1, {{"cloth", 2}});
    auto* cons = player->AddComponent<Consumables>();
    cons->AddRecipe("bandage", "Heal", 35.0f);   // index 0 -> Use heals 35

    // ---- Wolves: a template + dens that spawn them over time ----
    Wolf(scene, -26.0f, -24.0f);     // the "Wolf" blueprint (hidden by the spawners)
    for (int i = 0; i < 2; ++i) {
        GameObject* den = scene.CreateGameObject("Den");
        den->transform->localPosition = {i ? 20.0f : -20.0f, 0.2f, i ? 16.0f : -16.0f};
        auto* mr = den->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Sphere(); mr->color = Color::FromBytes(60, 55, 50);
        den->transform->localScale = {1.5f, 0.6f, 1.5f};
        auto* sp = den->AddComponent<Spawner>();
        sp->templateName = "Wolf"; sp->interval = 12.0f; sp->maxAlive = 2;
        sp->spawnRadius = 3.0f; sp->startDelay = 8.0f;
    }

    // Let the player fight back, and a toggleable craft menu (press C).
    player->AddComponent<MeleeAttacker>();   // F or left-mouse to swing
    auto* menu = player->AddComponent<CraftingMenu>();
    menu->toggleKey = 'c'; menu->position = {20.0f, 110.0f};

    // ---- Camera (the controller orbits it behind the player) ----
    GameObject* camObj = scene.CreateGameObject("Main Camera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    cam->main = true;
    camObj->transform->localPosition = {0, 3, 10};

    // ---- HUD: stat bars + clock ----
    Bar(scene, "HealthBar",   20.0f, Color::FromBytes(210,  70,  70));
    Bar(scene, "HungerBar",   48.0f, Color::FromBytes(210, 150,  60));
    Bar(scene, "ThirstBar",   76.0f, Color::FromBytes( 70, 150, 220));
    Bar(scene, "StaminaBar", 104.0f, Color::FromBytes( 90, 200, 110));
    Bar(scene, "OxygenBar",  132.0f, Color::FromBytes(120, 200, 230));

    // Use button (the craft buttons are auto-built by the player's CraftingMenu, press C).
    {
        GameObject* b = scene.CreateGameObject("UseBandageBtn");
        auto* btn = b->AddComponent<UIButton>();
        btn->label = "Use Bandage"; btn->position = {20.0f, 60.0f}; btn->size = {150.0f, 34.0f};
        btn->anchor = UIAnchor::BottomRight;
        btn->clickTarget = "Player"; btn->clickFunction = "UseItem"; btn->clickArg = 0.0f; // consumable 0
    }

    GameObject* clock = scene.CreateGameObject("Clock");
    auto* tr = clock->AddComponent<TextRenderer>();
    tr->screenPos = {20.0f, 168.0f}; tr->pixelSize = 0.18f; tr->anchor = UIAnchor::TopLeft;
    clock->AddComponent<UITextBind>()->format = "Time {hour}h";

    GameObject* hint = scene.CreateGameObject("Hint");
    auto* ht = hint->AddComponent<TextRenderer>();
    ht->screenPos = {20.0f, 16.0f}; ht->pixelSize = 0.14f; ht->anchor = UIAnchor::BottomLeft;
    ht->text = "WASD move, Shift run, Space jump, F / click to attack. Eat at the bush,\n"
               "drink at the well, warm at the fire. Fight the wolves. Press C to craft\n"
               "a bandage (from cloth), then Use Bandage (bottom-right) to heal. Survive!";

    // ---- Write the game files ----
    if (!SceneSerializer::SaveToFile(scene, outDir + "game.okayscene")) {
        std::fprintf(stderr, "failed to write game.okayscene\n");
        return 1;
    }
    FILE* cf = std::fopen((outDir + "game.okayconfig").c_str(), "wb");
    if (!cf) { std::fprintf(stderr, "failed to write game.okayconfig\n"); return 1; }
    std::fprintf(cf,
        "title=OkaySurvival\n" "width=1100\n" "height=680\n"
        "vsync=1\n" "lock_cursor=1\n" "quit_on_escape=1\n" "show_fps=1\n"
        "startup=game.okayscene\n" "scene=game.okayscene\n");
    std::fclose(cf);
    std::printf("OkaySurvival written to %s\n", outDir.c_str());
    return 0;
}
