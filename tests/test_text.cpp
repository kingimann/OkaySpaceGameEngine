#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("text");

    // --- Font: space is blank, printable glyphs have set pixels ---
    {
        bool spaceBlank = true;
        for (int y = 0; y < Font8x8::Height; ++y)
            for (int x = 0; x < Font8x8::Width; ++x)
                if (Font8x8::Pixel(' ', x, y)) spaceBlank = false;
        CHECK(spaceBlank);

        auto hasInk = [](char c) {
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    if (Font8x8::Pixel(c, x, y)) return true;
            return false;
        };
        CHECK(hasInk('A'));
        CHECK(hasInk('0'));
        CHECK(hasInk('?'));

        // Out-of-cell coordinates are safely off.
        CHECK(!Font8x8::Pixel('A', -1, 0));
        CHECK(!Font8x8::Pixel('A', 0, 99));
    }

    // --- MeasureWidth scales with string length ---
    {
        CHECK(Font8x8::MeasureWidth("") == 0);
        CHECK(Font8x8::MeasureWidth("ABC") == 3 * Font8x8::Width);
    }

    // --- TextRenderer width/height helpers ---
    {
        Scene scene("T");
        GameObject* go = scene.CreateGameObject("Label");
        auto* tr = go->AddComponent<TextRenderer>();
        tr->text = "SCORE";   // 5 glyphs advance-spaced: 4*(W+1) + W
        CHECK(tr->PixelWidth() == 4 * (Font8x8::Width + 1) + Font8x8::Width);
        CHECK(tr->PixelHeight() == Font8x8::Height);

        // letterSpacing widens; uppercase changes the drawn text length-neutrally.
        tr->letterSpacing = 2.0f;
        CHECK(tr->PixelWidth() == 4 * (Font8x8::Width + 1 + 2) + Font8x8::Width);
        tr->letterSpacing = 0.0f;
    }

    // --- TextRenderer survives serialization (text with a space) ---
    {
        Scene scene("Ser");
        GameObject* go = scene.CreateGameObject("HUD");
        auto* tr = go->AddComponent<TextRenderer>();
        tr->text = "Score: 0";
        tr->color = Color::Yellow;
        tr->pixelSize = 0.2f;
        tr->screenSpace = true;
        tr->screenPos = {32.0f, 16.0f};

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("HUD")->GetComponent<TextRenderer>();
        CHECK(r != nullptr);
        CHECK(r->text == "Score: 0");
        CHECK(r->screenSpace);
        CHECK_NEAR(r->pixelSize, 0.2f, 0.001f);
        CHECK_NEAR(r->screenPos.x, 32.0f, 0.001f);
    }

    // --- Screen-space anchor resolves against the text's own size ---
    {
        Scene scene("Anchor");
        GameObject* go = scene.CreateGameObject("Title");
        auto* tr = go->AddComponent<TextRenderer>();
        tr->text = "HI";                 // width = 2 glyphs
        tr->pixelSize = 2.0f;            // rendered glyph width = 2 * 8 = 16 px each
        tr->screenSpace = true;
        tr->screenPos = {0, 0};
        tr->align = 0; tr->vcenter = false;            // top-left inside the box

        float tw = tr->PixelWidth() * tr->pixelSize;   // 2*8*2 = 32
        float th = tr->PixelHeight() * tr->pixelSize;  // 8*2  = 16
        tr->size = {tw, th};             // box == text, so anchoring uses that size

        // TopLeft: offset unchanged.
        tr->anchor = UIAnchor::TopLeft;
        Vec2 tl = tr->ResolvedScreenPos(800, 600);
        CHECK_NEAR(tl.x, 0.0f, 0.001f);
        CHECK_NEAR(tl.y, 0.0f, 0.001f);

        // Center: the text block is centered on the canvas.
        tr->anchor = UIAnchor::Center;
        Vec2 c = tr->ResolvedScreenPos(800, 600);
        CHECK_NEAR(c.x, (800 - tw) * 0.5f, 0.001f);
        CHECK_NEAR(c.y, (600 - th) * 0.5f, 0.001f);

        // BottomRight: text hugs the corner (its size pulled inside).
        tr->anchor = UIAnchor::BottomRight;
        Vec2 br = tr->ResolvedScreenPos(800, 600);
        CHECK_NEAR(br.x, 800 - tw, 0.001f);
        CHECK_NEAR(br.y, 600 - th, 0.001f);
    }

    // --- Text shadow round-trips through serialization ---
    {
        Scene scene("Shadow");
        GameObject* go = scene.CreateGameObject("HUD");
        auto* tr = go->AddComponent<TextRenderer>();
        tr->text = "Lives";
        tr->shadow = true;
        tr->shadowColor = Color(0.0f, 0.0f, 0.0f, 0.5f);
        tr->shadowOffset = {3.0f, 2.0f};
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("HUD")->GetComponent<TextRenderer>();
        CHECK(r != nullptr);
        CHECK(r->shadow == true);
        CHECK_NEAR(r->shadowColor.a, 0.5f, 0.01f);
        CHECK_NEAR(r->shadowOffset.x, 3.0f, 0.001f);
        CHECK_NEAR(r->shadowOffset.y, 2.0f, 0.001f);
    }

    // --- A text line with anchor but no shadow block still loads (back-compat) ---
    {
        std::string text =
            "okayscene 1\nname \"S\"\ngravity 0 0\n"
            "gameobject 0 \"T\"\n  active 1\n  parent -1\n"
            "  text \"Hi\" 1 1 1 1 0.1 1 12 12 4\n"   // ...screenPos + anchor(4), no shadow
            "end\n";
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("T")->GetComponent<TextRenderer>();
        CHECK(r != nullptr);
        CHECK(r->anchor == UIAnchor::Center);
        CHECK(r->shadow == false);            // defaulted (no trailing block)
    }

    // --- Text anchor round-trips through serialization ---
    {
        Scene scene("AnchorSer");
        GameObject* go = scene.CreateGameObject("HUD");
        auto* tr = go->AddComponent<TextRenderer>();
        tr->text = "x";
        tr->screenSpace = true;
        tr->anchor = UIAnchor::BottomCenter;
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        CHECK(loaded.Find("HUD")->GetComponent<TextRenderer>()->anchor == UIAnchor::BottomCenter);
    }

    // --- Screen-space text is a resizable UI rect (the box); box fields save ---
    {
        Scene scene("Box");
        GameObject* go = scene.CreateGameObject("Label");
        auto* tr = go->AddComponent<TextRenderer>();
        tr->screenSpace = true;
        tr->size = {300.0f, 64.0f};
        tr->background = true;
        tr->backgroundColor = Color(0.1f, 0.2f, 0.3f, 0.8f);
        UIRect r = GetUIRect(go);
        CHECK(r.valid);
        CHECK(r.sizePtr == &tr->size);          // resize handles edit the box
        CHECK_NEAR(r.size.x, 300.0f, 0.001f);
        CHECK_NEAR(r.size.y, 64.0f, 0.001f);
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L"); std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* rr = loaded.Find("Label")->GetComponent<TextRenderer>();
        CHECK_NEAR(rr->size.x, 300.0f, 0.001f);
        CHECK(rr->background == true);
        CHECK_NEAR(rr->backgroundColor.b, 0.3f, 0.01f);
    }

    TEST_MAIN_RESULT();
}
