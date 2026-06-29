#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Character can be rebuilt as a hierarchy of editable part objects that assemble
// into the same character and animate via the part transforms.
static int silhouette(Scene& s) {
    const int W = 200, H = 200;
    Mat4 vp = Mat4::Perspective(60.0f * 3.14159265f / 180.0f, 1.0f, 0.1f, 100.0f)
            * Mat4::LookAt({0, 1.0f, 3.2f}, {0, 1.0f, 0}, {0, 1, 0});
    Raster work; std::vector<std::uint32_t> out;
    const std::uint32_t* px = RenderMeshesSS(work, out, s, vp, {0, 1.0f, 3.2f}, W, H, 1, nullptr);
    int lit = 0; for (int i = 0; i < W * H; ++i) if (px[i] & 0xFF000000u) ++lit;
    return lit;
}

int main() {
    RUN_SUITE("character_parts");

    // Baked single-mesh character (reference silhouette).
    int baked = 0;
    {
        Scene s("Baked");
        auto* ch = s.CreateGameObject("Player")->AddComponent<Character>();
        ch->anim = 1; ch->Apply();
        s.Start(); s.Update(0.016f);
        baked = silhouette(s);
        CHECK(baked > 1000);
    }

    // Separated into parts: same character, now many editable objects.
    {
        Scene s("Parts");
        GameObject* go = s.CreateGameObject("Player");
        auto* ch = go->AddComponent<Character>();
        ch->anim = 1; ch->Apply();
        ch->separateParts = true;
        s.Start();
        for (int i = 0; i < 3; ++i) s.Update(0.016f);

        CHECK(ch->PartsBuilt());
        CHECK(ch->Part(0) != nullptr);                 // Hips
        CHECK(ch->Part(2) != nullptr);                 // Head
        // The baked mesh is hidden; the parts render instead.
        auto* mr = go->GetComponent<MeshRenderer>();
        CHECK(mr && mr->enabled == false);
        // Each part is a real editable object with its own mesh.
        if (ch->Part(2)) {
            auto* hmr = ch->Part(2)->GetComponent<MeshRenderer>();
            CHECK(hmr && !hmr->mesh.vertices.empty());
        }

        int parts = silhouette(s);
        CHECK(parts > 1000);
        // The assembled parts cover roughly the same area as the baked character.
        float ratio = (float)parts / (float)(baked > 0 ? baked : 1);
        CHECK(ratio > 0.7f && ratio < 1.4f);
    }

    TEST_MAIN_RESULT();
}
