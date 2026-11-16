#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Render/OcclusionOverlay.hpp"
#include "okay/Core/FrameOverlay.hpp"
#include "okay/Scene/SceneTemplates.hpp"
#include <cstring>
#include <iostream>

using namespace okay;

// The real cause of "my fps drops when I dig" in the Sandbox: the first dig creates a ParticleSystem for the dust. A GPU host
// (the player, the editor) runs a software depth pass of the WHOLE scene at half resolution, with shadows, every frame while any
// ParticleSystem exists, even one with no live particle. That pass cost about as much as a software frame (200+ ms here).
// Now: it runs only for live particles / lines / trails, and it draws without shadows, post effects or tone mapping.
int main() {
    RUN_SUITE("voxel_overlay_depth");
    VoxelTerrain::ForceOldRemesh(0);

    // --- an idle particle system does not count; a live one, a line and a trail do ---------------------------------------------------
    {
        Scene s("O"); s.physicsEnabled = false;
        CHECK(!SceneHasOverlayNeedingDepth(s));
        GameObject* g = s.CreateGameObject("Dust");
        auto* ps = g->AddComponent<ParticleSystem>();
        ps->playing = false; ps->emissionRate = 0.0f;
        s.Update(0.1f);
        CHECK(!SceneHasOverlayNeedingDepth(s));                       // exists, nothing alive
        ps->Emit(4); s.Update(0.01f);
        CHECK(SceneHasOverlayNeedingDepth(s));                        // four live particles
        for (int i = 0; i < 400; ++i) s.Update(0.05f);                // they die
        CHECK(!SceneHasOverlayNeedingDepth(s));
        s.CreateGameObject("Line")->AddComponent<LineRenderer>();
        CHECK(SceneHasOverlayNeedingDepth(s));                        // lines and trails still count, as before
    }

    // --- in the Sandbox: before the first dig nothing; during a dig yes; some time after releasing the button, no again --------------------
    {
        Scene s("SB"); Templates::VoxelSandbox(s); s.runScripts = true; s.physicsEnabled = true;
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(!SceneHasOverlayNeedingDepth(s));
        GameObject* cam = s.Find("FPS Camera");
        cam->transform->SetParent(nullptr, true);
        float sy = 0; s.Find("Terrain")->GetComponent<VoxelTerrain>()->SurfaceY(0, 0, sy);
        cam->transform->localPosition = {0, sy + 6.0f, 0};
        cam->transform->localRotation = Quat::LookRotation(Vec3{0, 1, 0}, Vec3{0, 0, -1});
        Input::FeedMouse({0, 0}, 1u);
        bool during = false;
        for (int i = 0; i < 30; ++i) { s.Update(1.0f / 60.0f); during = during || SceneHasOverlayNeedingDepth(s); }
        Input::FeedMouse({0, 0}, 0u);
        CHECK(s.Find("VoxelDebris") != nullptr);                       // the dust object exists from now on...
        CHECK(during);
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);          // 2 s later
        CHECK(!SceneHasOverlayNeedingDepth(s));                        // ...and no longer makes every frame pay
        // the terrain reported itself to the F3 overlay
        const VoxelOverlayStats& v = FrameOverlay::Voxel();
        std::cout << "  overlay: " << v.terrains << " terrain, " << v.triangles << " triangles, " << v.chunkObjects << " chunk objects of " << v.chunksTotal
                  << ", terrain " << v.updateMs << " ms, digger " << v.digMs << " ms\n";
        CHECK(v.terrains == 1 && v.triangles > 1000 && v.chunksTotal > 10 && v.chunkObjects > 0);
        CHECK(FrameOverlay::NowSeconds() - v.seenAt < 1.0);
    }

    // --- the cheaper depth pass publishes the same depth and leaves the settings as it found them -------------------------------------------
    {
        Scene s("D"); Templates::VoxelSandbox(s); s.physicsEnabled = false;
        ApplySceneLight(s);
        float sy = 0; s.Find("Terrain")->GetComponent<VoxelTerrain>()->SurfaceY(0, 0, sy);
        const Vec3 eye{0, sy + 1.62f, 0};
        const Mat4 vp = Mat4::Perspective(60.0f, 16.0f / 9.0f, 0.1f, 600.0f) * Mat4::LookAt(eye, Vec3{0, sy + 1.0f, 30}, Vec3{0, 1, 0});
        const bool sh = ShadowsEnabled(), fx = FXAAEnabled(), tm = ToneMapEnabled();
        Raster a, b; std::vector<std::uint32_t> da, db;
        ShadowsEnabled() = true;
        RenderMeshesSS(a, da, s, vp, eye, 320, 180, 1, nullptr);
        const OcclusionDepth full = SceneOcclusionDepth();
        FillOcclusionDepth(b, db, s, vp, eye, 320, 180, nullptr);
        const OcclusionDepth lean = SceneOcclusionDepth();
        // With shadows on, lit meshes take the per-pixel path; without, the per-vertex one. Pixels along triangle edges can come out a little
        // different, which a coarse particle-occlusion test does not mind: what matters is the same pixels are covered and the depth agrees.
        std::size_t covered = 0, mismatch = 0, far = 0;
        for (std::size_t i = 0; i < full.d.size(); ++i) {
            const bool a = full.d[i] > 0.0f, b = lean.d[i] > 0.0f;
            if (a) ++covered;
            if (a != b) { ++mismatch; continue; }
            if (a && std::fabs(full.d[i] - lean.d[i]) > 0.02 * full.d[i]) ++far;   // depth off by more than 2 percent
        }
        std::cout << "  depth pass: " << covered << " pixels covered, " << mismatch << " coverage differences, " << far << " depths more than 2 percent apart\n";
        CHECK(covered > 20000);
        CHECK(mismatch * 100 < covered * 3);                            // under 3 percent of the covered pixels flip (edges)
        CHECK(far * 100 < covered * 5);                                 // and under 5 percent disagree about how far
        CHECK(ShadowsEnabled() == true && FXAAEnabled() == fx && ToneMapEnabled() == tm);        // settings put back
        ShadowsEnabled() = sh;
    }

    TEST_MAIN_RESULT();
}
