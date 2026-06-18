#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>

using namespace okay;

int main() {
    RUN_SUITE("image");

    // --- Construct, set pixels, read them back ---
    {
        Image img(4, 3);
        CHECK(img.Valid());
        CHECK(img.Width() == 4 && img.Height() == 3);
        img.Fill(Color(0, 0, 0, 1));
        img.SetPixel(1, 2, Color(1, 0, 0, 1));   // red
        img.SetPixel(3, 0, Color(0, 1, 0, 0.5f)); // semi-transparent green
        Color a = img.GetPixel(1, 2);
        CHECK_NEAR(a.r, 1.0f, 0.01f);
        CHECK_NEAR(a.g, 0.0f, 0.01f);
        Color b = img.GetPixel(3, 0);
        CHECK_NEAR(b.g, 1.0f, 0.01f);
        CHECK_NEAR(b.a, 0.5f, 0.01f);
        // Out-of-bounds reads are transparent black, not a crash.
        CHECK(img.GetPixel(-1, 0).a == 0.0f);
        CHECK(img.GetPixel(99, 99).a == 0.0f);
    }

    // --- PNG save/load round-trip preserves dimensions and pixels ---
    {
        Image img(8, 6);
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 8; ++x)
                img.SetPixel(x, y, Color(x / 7.0f, y / 5.0f, 0.25f, 1.0f));

        const char* path = "test_image.png";
        CHECK(img.SavePNG(path));

        Image loaded;
        std::string err;
        bool ok = loaded.Load(path, &err);
        CHECK(ok);
        if (!ok) std::cerr << "  load error: " << err << "\n";
        CHECK(loaded.Width() == 8 && loaded.Height() == 6);
        // Spot-check a few pixels survived the PNG round-trip (8-bit exact).
        Color c = loaded.GetPixel(7, 5);
        CHECK_NEAR(c.r, 1.0f, 0.02f);
        CHECK_NEAR(c.g, 1.0f, 0.02f);
        Color c0 = loaded.GetPixel(0, 0);
        CHECK_NEAR(c0.r, 0.0f, 0.02f);
        CHECK_NEAR(c0.b, 0.25f, 0.02f);

        std::remove(path);
    }

    // --- Loading a missing file fails cleanly ---
    {
        Image img;
        CHECK(!img.Load("definitely_missing.png"));
        CHECK(!img.Valid());
    }

    // --- SpriteRenderer.texture survives scene serialization ---
    {
        Scene scene("Tex");
        GameObject* go = scene.CreateGameObject("Sprite");
        auto* sr = go->AddComponent<SpriteRenderer>();
        sr->texture = "player.png";
        sr->color = Color::Red;

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("Sprite")->GetComponent<SpriteRenderer>();
        CHECK(r != nullptr);
        CHECK(r->texture == "player.png");
    }

    TEST_MAIN_RESULT();
}
