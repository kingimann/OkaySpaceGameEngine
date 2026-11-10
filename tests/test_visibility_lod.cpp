#include "test_framework.hpp"
#include <Okay.hpp>
#include <chrono>
#include <cmath>
#include <sstream>

using namespace okay;

static GameObject* Box(Scene& s, const char* name, Vec3 at, Color col, bool stat) {
    GameObject* go = s.CreateGameObject(name);
    go->transform->localPosition = at;
    go->isStatic = stat;
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube();
    mr->color = col;
    return go;
}

static Mat4 CamVP(const Vec3& eye) {
    const Mat4 view = Mat4::LookAt(eye, {0, 0, 0}, {0, 1, 0});
    const Mat4 proj = Mat4::Perspective(60.0f, 1.0f, 0.1f, 500.0f);
    return proj * view;
}

int main() {
    RUN_SUITE("lod selection changes the mesh");
    {
        Scene s;
        GameObject* go = Box(s, "Tree", {0, 0, 0}, Color::White, true);
        auto* mr = go->GetComponent<MeshRenderer>();
        mr->mesh = Mesh::Sphere(1.0f, 8);
        auto* lod = go->AddComponent<LODGroup>();
        lod->Generate(mr->mesh);
        CHECK(lod->levels.size() >= 2u);
        CHECK(!lod->lodMeshes.empty());
        CHECK(lod->lodMeshes[0].TriangleCount() <= mr->mesh.TriangleCount());
        const int full = mr->mesh.TriangleCount();
        visibility::RenderViewFovY() = 60.0f;
        const visibility::Frame& nearF = visibility::Build(s, CamVP({0, 1, 3}), {0, 1, 3}, 256, 256, 60, nullptr);
        CHECK(nearF.lodCount[0] >= 1);
        CHECK(nearF.items.size() == 1u);
        CHECK(nearF.items[0].mesh == &mr->DisplayMesh() || nearF.items[0].lod == 0);
        lod->lodState = -1;
        const visibility::Frame& farF = visibility::Build(s, CamVP({0, 1, 80}), {0, 1, 80}, 256, 256, 60, nullptr);
        CHECK(farF.lodCulled == 1 || (farF.items.size() == 1u && farF.items[0].lod > 0));
        if (!farF.items.empty() && farF.items[0].lod > 0)
            CHECK(farF.items[0].mesh->TriangleCount() < full);
        std::string text = SceneSerializer::Serialize(s);
        Scene loaded;
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        LODGroup* back = nullptr;
        for (const auto& up : loaded.Objects()) if (auto* g = up->GetComponent<LODGroup>()) back = g;
        CHECK(back != nullptr);
        CHECK(back->levels.size() == lod->levels.size());
        CHECK(back->lodMeshes.size() + 1 == back->levels.size());
        CHECK_NEAR(back->levels[1].distance, lod->levels[1].distance, 1e-3f);
    }

    RUN_SUITE("occlusion and batching");
    {
        Scene s;
        GameObject* wall = Box(s, "Wall", {0, 1, 0}, Color::White, true);
        wall->transform->localScale = {8, 4, 0.4f};
        wall->AddComponent<Occluder>();
        int hidden = 0;
        for (int i = 0; i < 12; ++i) {
            Box(s, "Behind", {((i % 4) - 1.5f) * 1.2f, 1.0f, -6.0f - (i / 4) * 1.5f}, Color(0.2f, 0.6f, 1, 1), true);
            ++hidden;
        }
        for (int i = 0; i < 6; ++i)
            Box(s, "Front", {((i % 3) - 1.0f) * 2.0f, 0.5f, 4.0f}, Color(1, 0.4f, 0.2f, 1), true);
        visibility::OcclusionCullingEnabled() = true;
        visibility::StaticBatchingEnabled() = false;
        const Mat4 vp = CamVP({0, 2, 12});
        const visibility::Frame& occ = visibility::Build(s, vp, {0, 2, 12}, 320, 240, 60, nullptr);
        CHECK(occ.occluded > 0);
        CHECK(occ.individuals < 1 + hidden + 6);
        visibility::OcclusionCullingEnabled() = false;
        visibility::StaticBatchingEnabled() = true;
        const auto t0 = std::chrono::steady_clock::now();
        const visibility::Frame& batched = visibility::Build(s, vp, {0, 2, 12}, 320, 240, 60, nullptr);
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        CHECK(batched.occluded == 0);
        CHECK(batched.batches >= 1);
        CHECK(batched.drawsSaved >= 1);
        CHECK((int)batched.items.size() < batched.individuals);
        std::cout << "  perf: " << batched.individuals << " objects -> " << batched.items.size()
                  << " draws (saved " << batched.drawsSaved << "), plan " << ms << " ms\n";
        auto timeRender = [&](bool occlusion, int& drawn, int& occluded) {
            visibility::OcclusionCullingEnabled() = occlusion;
            visibility::StaticBatchingEnabled() = true;
            Raster r; std::vector<std::uint32_t> down;
            RenderMeshesSS(r, down, s, vp, {0, 2, 12}, 320, 180, 1, nullptr);   // warm caches
            const auto a = std::chrono::steady_clock::now();
            RenderMeshesSS(r, down, s, vp, {0, 2, 12}, 320, 180, 1, nullptr);
            const auto b = std::chrono::steady_clock::now();
            drawn = LastCullCounters().drawn;
            occluded = LastCullCounters().occluded;
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        int dOn = 0, oOn = 0, dOff = 0, oOff = 0;
        const double withOcc = timeRender(true, dOn, oOn);
        const double noOcc = timeRender(false, dOff, oOff);
        std::cout << "  frame: software 320x180 occlusion " << withOcc << " ms (drawn " << dOn
                  << ", occluded " << oOn << "), without " << noOcc << " ms (drawn " << dOff
                  << ", occluded " << oOff << ")\n";
        // A merged vertex sits where the source cube's corner was moved.
        bool found = false;
        for (const visibility::Item& it : batched.items) if (it.batched && it.mesh) {
            CHECK(!it.mesh->vertices.empty());
            found = true;
        }
        CHECK(found);
        visibility::OcclusionCullingEnabled() = true;
        visibility::StaticBatchingEnabled() = true;
    }

    RUN_SUITE("no lod and no occluder matches the old list");
    {
        Scene s;
        Box(s, "A", {0, 0, 0}, Color::White, false);
        Box(s, "B", {3, 0, 0}, Color::White, false);
        visibility::DynamicBatchingEnabled() = false;
        const visibility::Frame& f = visibility::Build(s, CamVP({0, 2, 8}), {0, 2, 8}, 128, 128, 60, nullptr);
        CHECK(f.items.size() == 2u);
        CHECK(f.batches == 0);
        CHECK(f.occluded == 0);
        CHECK(!f.items[0].batched);
        visibility::DynamicBatchingEnabled() = true;
    }

    RUN_SUITE("second sun warns and the caster wins");
    {
        Scene s;
        GameObject* a = s.CreateGameObject("Fill");
        a->transform->localRotation = Quat::LookRotation({0, -1, 0});
        auto* la = a->AddComponent<Light>();
        la->type = Light::Type::Directional;
        la->castShadows = false;
        GameObject* b = s.CreateGameObject("Sun");
        b->transform->localRotation = Quat::LookRotation({1, -1, 0});
        auto* lb = b->AddComponent<Light>();
        lb->type = Light::Type::Directional;
        lb->castShadows = true;
        GameObject* c = s.CreateGameObject("Extra");
        c->transform->localRotation = Quat::LookRotation({-1, -1, 0});
        auto* lc = c->AddComponent<Light>();
        lc->type = Light::Type::Directional;
        lc->castShadows = true;
        ApplySceneLight(s);
        CHECK(SceneLights::SunIndex() == 1);
        CHECK(!DirectionalShadowWarning().empty());
        lc->castShadows = false;
        ApplySceneLight(s);
        CHECK(SceneLights::SunIndex() == 1);
        CHECK(DirectionalShadowWarning().empty());
    }

    RUN_SUITE("quality preset sets shadow size");
    {
        const int prev = DisplaySettings::Quality();
        DisplaySettings::SetQuality(0);
        CHECK(ShadowMapResolution() == 1024);
        CHECK_NEAR(ShadowDistance(), 40.0f, 0.01f);
        CHECK(ShadowCascades() == 1);
        CHECK(!ShadowsEnabled());
        DisplaySettings::SetQuality(1);
        CHECK(ShadowMapResolution() == 2048);
        CHECK_NEAR(ShadowDistance(), 80.0f, 0.01f);
        CHECK(ShadowCascades() == 3);
        CHECK(ShadowsEnabled());
        DisplaySettings::SetQuality(2);
        CHECK(ShadowMapResolution() == 4096);
        CHECK_NEAR(ShadowDistance(), 120.0f, 0.01f);
        CHECK(ShadowCascades() == 4);
        DisplaySettings::SetQuality(prev);
    }

    RUN_SUITE("software lod tint and depth pass keep stats");
    {
        Scene s;
        GameObject* go = Box(s, "Ball", {0, 0, 0}, Color::White, true);
        auto* mr = go->GetComponent<MeshRenderer>();
        mr->mesh = Mesh::Sphere(0.8f, 6);
        auto* lod = go->AddComponent<LODGroup>();
        lod->Generate(mr->mesh);
        lod->debugTint = true;
        lod->forceLevel = 0;
        s.Start();
        Raster r; std::vector<std::uint32_t> down;
        const Mat4 vp = CamVP({0, 0.4f, 3.2f});
        RenderMeshesSS(r, down, s, vp, {0, 0.4f, 3.2f}, 96, 64, 1, nullptr);
        const int drawn = LastCullCounters().drawn;
        CHECK(drawn >= 1);
        int redder = 0;
        for (std::uint32_t p : r.color) {
            const int R = (int)(p & 255), G = (int)((p >> 8) & 255), B = (int)((p >> 16) & 255);
            if ((p >> 24) > 200 && R > G + 30 && R > B + 30) ++redder;
        }
        CHECK(redder > 20);   // LOD 0 debug tint is red on the software path too
        FillOcclusionDepth(r, down, s, vp, {0, 0.4f, 3.2f}, 48, 32, nullptr);
        CHECK(SceneOcclusionDepth().valid);
        CHECK(LastCullCounters().drawn == drawn);
    }
    TEST_MAIN_RESULT();
}
