#include "test_framework.hpp"
#include <Okay.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>

using namespace okay;

// Terrain::Apply and Water's per-frame rebuild welded every vertex through a std::map just to find out that no two share
// a position. The grid normals are now added up without it (okay/Render/MeshNormals.hpp): the very same numbers, much faster.
// docs/rendering/terrain_audit.md has the timings.
namespace {

bool SameNormals(const Mesh& a, const Mesh& b) {
    return a.normals.size() == b.normals.size() &&
           (a.normals.empty() || std::memcmp(a.normals.data(), b.normals.data(), a.normals.size() * sizeof(Vec3)) == 0);
}

// What the mesh would have had before: the same vertices and triangles, normals from the welding function.
Mesh WithOldNormals(const Mesh& m) {
    Mesh o = m;
    o.normals.clear();
    o.ComputeSmoothNormals();
    return o;
}

double BestMs(const std::function<void()>& f, int reps) {
    double best = 1e30;
    for (int r = 0; r < 4; ++r) {
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < reps; ++i) f();
        best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps);
    }
    return best;
}

} // namespace

int main() {
    RUN_SUITE("terrain_water_speed");

    // --- heightmap Terrain: the normals are the ones ComputeSmoothNormals gave ---------------------------------------
    {
        struct Cfg { int res; float size; unsigned seed; };
        const Cfg cfgs[] = {{1, 10.0f, 1u}, {2, 10.0f, 2u}, {8, 50.0f, 3u}, {33, 80.0f, 4u}, {64, 50.0f, 5u}, {100, 300.0f, 6u},
                            {64, 0.1f, 7u} /* cells so small the welding rounding matters: falls back to the old function */};
        int bad = 0;
        for (const Cfg& c : cfgs) {
            Terrain t;
            t.Resize(c.res); t.size = c.size;
            t.Generate(c.seed % 7, 6.0f, 3.0f, 4, c.seed);
            t.RaiseAt(0.0f, 0.0f, c.size * 0.2f, 3.0f);
            const Mesh m = t.BuildMesh();
            CHECK(m.vertices.size() == (std::size_t)(c.res + 1) * (c.res + 1));
            CHECK(m.HasNormals());
            if (!SameNormals(m, WithOldNormals(m))) ++bad;
        }
        CHECK(bad == 0);
        // flat terrain: every normal is straight up
        Terrain f; f.Resize(16); f.Flatten(2.0f);
        const Mesh fm = f.BuildMesh();
        bool up = true;
        for (const Vec3& n : fm.normals) up = up && std::fabs(n.x) < 1e-6f && n.y > 0.999f && std::fabs(n.z) < 1e-6f;
        CHECK(up);
    }

    // --- Water: the normals of every animated frame are the old ones, too ----------------------------------------------
    {
        Scene s("W"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Water");
        auto* w = go->AddComponent<Water>();
        w->size = 40.0f; w->resolution = 24; w->depth = 0.0f; w->waveHeight = 1.0f;
        w->Apply();
        int bad = 0;
        for (int i = 0; i < 6; ++i) {
            s.Update(0.1f);
            const Mesh& m = go->GetComponent<MeshRenderer>()->mesh;
            CHECK(m.HasNormals() && m.vertices.size() == 25u * 25u);
            if (!SameNormals(m, WithOldNormals(m))) ++bad;
        }
        CHECK(bad == 0);
    }

    // --- much faster than the welding function on a big grid -----------------------------------------------------------
    {
        Terrain t;
        t.Resize(256); t.size = 100.0f; t.Generate(1, 10.0f, 3.0f, 5, 7u);
        Mesh m = t.BuildMesh();
        Mesh a = m, b = m;
        const double oldMs = BestMs([&] { a.normals.clear(); a.ComputeSmoothNormals(); }, 2);
        const double newMs = BestMs([&] { meshnormals::SmoothNoWeld(b); }, 2);
        std::cout << "  256 x 256 grid (66049 vertices): ComputeSmoothNormals " << oldMs << " ms, SmoothNoWeld " << newMs
                  << " ms (x" << oldMs / newMs << ")\n";
        CHECK(SameNormals(a, b));
        CHECK(newMs * 3.0 < oldMs);
    }

    TEST_MAIN_RESULT();
}
