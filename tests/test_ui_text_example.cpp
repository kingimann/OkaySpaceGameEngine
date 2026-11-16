// The shipped sample scene examples/ui_text/ui_text.okayscene: two characters (a Goblin Scout and an Orc Chief), each with a
// nameplate that is a child of it and shows its own name, level and health, and a wooden sign whose text uses the rich-text tags
// ([b] [i] [color] [size]). A small script hurts and heals them (and does a few hits by itself at the start) so the health bars
// can be seen dropping. This test loads the file from disk and plays it, like a person pressing Play and the keys.
//
// The scene file is made by this test: run it with OKAY_WRITE_UI_TEXT_EXAMPLE=<path to ui_text.okayscene> to write it again
// (after a change to the nameplate or to the serializer). Without it the test only reads the committed file.
// Written for this engine; no other project's code or assets were used (the scene is flat coloured boxes and text).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/UIOwnerData.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/UIGallery.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace okay;

namespace {

// Pressing 1 hurts the Goblin, 2 the Orc, 3 gives the Goblin experience (a level up), H heals both. After a second the Goblin also
// takes four small hits by itself, so the bar drops without a key being pressed.
const char* kDirector =
    "var t = 0;\n"
    "var hits = 0;\n"
    "function update(dt) {\n"
    "  t = t + dt;\n"
    "  if (key_down(\"1\")) { hurt(\"Goblin Scout\", 8); }\n"
    "  if (key_down(\"2\")) { hurt(\"Orc Chief\", 12); }\n"
    "  if (key_down(\"3\")) { add_xp(\"Goblin Scout\", 100); }\n"
    "  if (key_down(\"h\")) { heal(\"Goblin Scout\", 100); heal(\"Orc Chief\", 200); }\n"
    "  if (hits < 4) { if (t > 1.2 + hits * 1.2) { hurt(\"Goblin Scout\", 6); hits = hits + 1; } }\n"
    "}\n";

// The sign: a big bold title, gold, red bold + italic, and a small line. Every tag the world text reads is in it.
const char* kSignText =
    "[size=160][b]OKAY TOWN[/b][/size]\n"
    "[color=#ffd040]Goblins and orcs ahead[/color]\n"
    "[color=#ff6060][b]Danger![/b][/color] [i]watch the bars[/i]\n"
    "[size=70]1 2 hurt   3 level up   H heal[/size]";

const char* kHints = "Press Play.   1 hurts the Goblin    2 hurts the Orc    3 levels the Goblin up    H heals both";

// Builds the scene the way the file holds it.
void BuildExample(Scene& s) {
    s.physicsEnabled = false;
    auto sprite = [&](const char* name, Vec2 at, Vec2 size, Color col, int order) {
        GameObject* g = s.CreateGameObject(name);
        g->transform->localPosition = {at.x, at.y, 0.0f};
        auto* sr = g->AddComponent<SpriteRenderer>();
        sr->size = size; sr->color = col; sr->sortOrder = order;
        return g;
    };
    {   // a flat (orthographic) camera: world text is drawn in flat scenes
        GameObject* g = s.CreateGameObject("Main Camera");
        g->transform->localPosition = {0.0f, 0.0f, -10.0f};
        auto* c = g->AddComponent<Camera>();
        c->projection = Camera::Projection::Orthographic;
        c->orthographicSize = 5.0f;
        c->clearFlags = Camera::ClearFlags::SolidColor;
        c->backgroundColor = Color::FromBytes(120, 168, 214);
        c->main = true;
    }
    sprite("Ground", {0.0f, -3.9f}, {24.0f, 2.2f}, Color::FromBytes(74, 130, 66), 0);
    sprite("Sign Post", {0.0f, -0.65f}, {0.5f, 4.3f}, Color::FromBytes(92, 60, 36), 0);   // stands on the ground
    sprite("Sign Board", {0.0f, 2.7f}, {10.2f, 2.4f}, Color::FromBytes(70, 45, 28), 1);
    {
        GameObject* g = s.CreateGameObject("Sign Text");
        g->transform->localPosition = {-4.6f, 3.65f, 0.0f};     // the top-left of the text
        auto* tr = g->AddComponent<TextRenderer>();
        tr->screenSpace = false;                                  // world text: it sits on the sign
        tr->richText = true;
        tr->pixelSize = 0.045f;                                   // world units per font pixel
        tr->color = Color::FromBytes(245, 240, 225);
        tr->shadow = true; tr->shadowColor = Color::FromBytes(20, 12, 6, 220);
        tr->text = kSignText;
    }
    auto character = [&](const char* name, Vec2 at, Vec2 size, Color col, int level, float hp) {
        GameObject* g = sprite(name, at, size, col, 2);
        auto* st = g->AddComponent<Stats>();
        st->level = level; st->health = hp; st->maxHealth = hp; st->defense = 0;   // damage is what the script says
        GameObject* plate = UIGallery::AddNameplate(s, true);    // a nameplate in the world...
        plate->transform->SetParent(g->transform, false);        // ...that is a child of the character: it shows this one
        plate->transform->localPosition = {0.0f, size.y * 0.5f + 0.25f, 0.0f};
        return g;
    };
    character("Goblin Scout", {-6.2f, -1.8f}, {1.5f, 2.0f}, Color::FromBytes(88, 166, 76), 3, 40.0f);
    character("Orc Chief", {6.2f, -1.5f}, {2.2f, 2.6f}, Color::FromBytes(166, 84, 64), 12, 120.0f);
    {   // the key hints, on the screen
        GameObject* canvas = UITemplates::EnsureCanvas(s);
        GameObject* hint = UITemplates::detail::Text(s, canvas, "Hints", kHints, UIAnchor::BottomCenter, {0.0f, -14.0f}, {1200.0f, 30.0f},
                                                     18.0f, Color::FromBytes(250, 246, 232), 1, true);
        auto* tr = hint->GetComponent<TextRenderer>();
        tr->shadow = true; tr->shadowColor = Color::FromBytes(10, 20, 10, 230);
    }
}

} // namespace

int main() {
    RUN_SUITE("ui_text_example");
    ActionList::ResetVars();
    const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/ui_text/ui_text.okayscene";

    if (const char* out = std::getenv("OKAY_WRITE_UI_TEXT_EXAMPLE")) {
        Scene s("Nameplates and a rich-text sign");
        BuildExample(s);
        s.Start();
        for (int i = 0; i < 2; ++i) s.Update(0.016f);              // the bindings write the names, levels and bars into the saved scene
        s.CreateGameObject("Director")->AddComponent<ScriptComponent>("okayscript")->LoadSource(kDirector);
        std::string err;
        const bool ok = SceneSerializer::SaveToFile(s, out, &err);
        std::printf("  wrote %s: %s %s\n", out, ok ? "ok" : "FAILED", err.c_str());
        CHECK(ok);
        TEST_MAIN_RESULT();
    }

    Scene s("play");
    std::string err;
    std::vector<std::string> warnings;
    const bool loaded = SceneSerializer::LoadFromFile(s, path, &err, &warnings);
    if (!loaded) std::printf("  could not load %s: %s\n", path.c_str(), err.c_str());
    CHECK(loaded);
    CHECK(warnings.empty());
    for (const auto& w : warnings) std::printf("  warning: %s\n", w.c_str());
    if (!loaded) TEST_MAIN_RESULT();

    GameObject* goblin = s.Find("Goblin Scout");
    GameObject* orc = s.Find("Orc Chief");
    GameObject* sign = s.Find("Sign Text");
    CHECK(goblin && orc && sign && s.Find("Director") && s.Find("Hints"));
    if (!(goblin && orc && sign)) TEST_MAIN_RESULT();

    {   // what the file holds: a flat scene, a rich-text world sign, two characters with their own plates
        Camera* cam = s.FindObjectOfType<Camera>();
        CHECK(cam && cam->projection == Camera::Projection::Orthographic);
        auto* tr = sign->GetComponent<TextRenderer>();
        CHECK(tr && !tr->screenSpace && tr->richText);
        if (tr) {
            const RichText rt = ParseRichText(tr->text);
            CHECK(rt.plain.find("OKAY TOWN") != std::string::npos && rt.plain.find("Goblins and orcs ahead") != std::string::npos);
            bool big = false, small = false, bold = false, italic = false, colour = false;
            for (const RichStyle& st : rt.styles) {
                big = big || st.size > 1.4f; small = small || st.size < 0.8f;
                bold = bold || st.bold; italic = italic || st.italic; colour = colour || st.hasColor;
            }
            CHECK(big && small && bold && italic && colour);                 // every tag the sign is there to show
            const std::vector<float> lines = RichLineScales(rt.plain, rt.styles);
            CHECK(lines.size() == 4 && lines[0] > 1.4f && lines[3] < 0.8f);  // a big title line and a small last line
        }
        for (GameObject* who : {goblin, orc}) {
            CHECK(who->GetComponent<Stats>() != nullptr);
            int plates = 0;
            for (Transform* c : who->transform->Children())
                if (c->gameObject->name == "Nameplate") {
                    ++plates;
                    auto* ws = c->gameObject->GetComponent<WorldSpaceUI>();
                    CHECK(ws && ws->constantSize);
                    CHECK(uiowner::Find(c->gameObject->transform->Find("SwatchPlate/Name")->gameObject) == who);   // the plate's owner is its parent
                }
            CHECK(plates == 1);
        }
    }

    // What a person sees after pressing Play, frame by frame (the script and the bindings run as in the game).
    const float dt = 0.016f;
    auto plateOf = [](GameObject* who) {
        struct P { TextRenderer* name; TextRenderer* level; UIProgressBar* bar; } p{nullptr, nullptr, nullptr};
        for (Transform* c : who->transform->Children()) {
            if (c->gameObject->name != "Nameplate") continue;
            if (Transform* t = c->Find("SwatchPlate/Name")) p.name = t->gameObject->GetComponent<TextRenderer>();
            if (Transform* t = c->Find("SwatchPlate/SwatchLevel/Label")) p.level = t->gameObject->GetComponent<TextRenderer>();
            if (Transform* t = c->Find("SwatchPlate/Health")) p.bar = t->gameObject->GetComponent<UIProgressBar>();
        }
        return p;
    };
    auto frames = [&](int n, std::vector<char> keys = {}) { for (int i = 0; i < n; ++i) { Input::FeedKeys(keys); s.Update(dt); } };
    const auto pg = plateOf(goblin), po = plateOf(orc);
    CHECK(pg.name && pg.level && pg.bar && po.name && po.level && po.bar);
    if (!(pg.name && pg.level && pg.bar && po.name && po.level && po.bar)) TEST_MAIN_RESULT();
    auto* sg = goblin->GetComponent<Stats>();
    auto* so = orc->GetComponent<Stats>();

    s.Start();
    frames(2);
    CHECK(pg.name->text == "Goblin Scout" && po.name->text == "Orc Chief");        // each plate shows its own character
    CHECK(pg.level->text == "Lv 3" && po.level->text == "Lv 12");
    CHECK_NEAR(pg.bar->value, 1.0f, 1e-4f);                                        // both start at full health
    CHECK_NEAR(po.bar->value, 1.0f, 1e-4f);

    frames(75);                                                                    // a little over a second
    CHECK(sg->health < 40.0f);                                                     // the script has hit the Goblin by itself
    CHECK_NEAR(pg.bar->value, sg->health / sg->maxHealth, 1e-4f);                  // and its bar dropped with it
    CHECK_NEAR(po.bar->value, 1.0f, 1e-4f);                                        // the Orc's plate did not move
    const float afterFirstHit = sg->health;

    frames(2, {'2'});                                                              // press 2: hurt the Orc
    CHECK_NEAR(so->health, 108.0f, 1e-3f);
    CHECK_NEAR(po.bar->value, 108.0f / 120.0f, 1e-4f);
    CHECK_NEAR(sg->health, afterFirstHit, 1e-3f);                                  // the Goblin is untouched by key 2
    frames(1);

    frames(2, {'1'});                                                              // press 1: hurt the Goblin
    CHECK_NEAR(sg->health, afterFirstHit - 8.0f, 1e-3f);
    CHECK_NEAR(pg.bar->value, sg->health / sg->maxHealth, 1e-4f);
    frames(1);

    frames(2, {'3'});                                                              // press 3: a level up (100 experience), health refilled
    CHECK(sg->level == 4);
    CHECK(pg.level->text == "Lv 4");
    CHECK(po.level->text == "Lv 12");
    CHECK_NEAR(pg.bar->value, 1.0f, 1e-4f);
    frames(1);

    frames(2, {'h'});                                                              // press H: heal both
    CHECK_NEAR(so->health, 120.0f, 1e-3f);
    CHECK_NEAR(po.bar->value, 1.0f, 1e-4f);
    Input::FeedKeys({});

    ActionList::ResetVars();
    TEST_MAIN_RESULT();
}
