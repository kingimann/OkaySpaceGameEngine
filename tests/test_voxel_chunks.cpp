#include "test_framework.hpp"
#include "voxel_mesh_util.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace okay;
using namespace voxeltest;

// The voxel edit path: the fast marching-cubes loop gives the very same mesh as the original, the dirty set marks
// exactly the chunks an edit reaches, the chunk objects the running game draws with look like one big mesh, and the
// brushes / floater cleanup still do what they always did.
namespace {

// ---- the brushes as they were before the rework (verbatim), to hold the new ones to -------------------------------
struct RefField {
    int nx, ny, nz; float vs, iso; int floorLayers; std::vector<float> d;
    float Clamp() const { return vs * 3.0f; }
    float HalfX() const { return (nx - 1) * vs * 0.5f; }
    float HalfZ() const { return (nz - 1) * vs * 0.5f; }
    int Index(int x, int y, int z) const { return x + y * nx + z * nx * ny; }
    bool InBounds(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < nx && y < ny && z < nz; }
    int Count() const { return nx * ny * nz; }
};
float RefClampTo(float v, float c) { return v < -c ? -c : (v > c ? c : v); }

bool RefDig(RefField& f, const Vec3& local, float radius, float amount) {
    float gx = (local.x + f.HalfX()) / f.vs, gy = local.y / f.vs, gz = (local.z + f.HalfZ()) / f.vs, gr = radius / f.vs;
    int x0 = std::max(0, (int)std::floor(gx - gr)), x1 = std::min(f.nx - 1, (int)std::ceil(gx + gr));
    int y0 = std::max(0, (int)std::floor(gy - gr)), y1 = std::min(f.ny - 1, (int)std::ceil(gy + gr));
    int z0 = std::max(0, (int)std::floor(gz - gr)), z1 = std::min(f.nz - 1, (int)std::ceil(gz + gr));
    bool changed = false;
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                if (f.floorLayers > 0 && amount > 0.0f && y < f.floorLayers) continue;
                float dx = x - gx, dy = y - gy, dz = z - gz;
                float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (d > gr) continue;
                float t = 1.0f - d / gr;
                float fall = t * t * (3.0f - 2.0f * t);
                float& cell = f.d[(std::size_t)f.Index(x, y, z)];
                float nv = RefClampTo(cell - amount * fall, f.Clamp());
                if (nv != cell) { cell = nv; changed = true; }
            }
    return changed;
}
bool RefSmooth(RefField& f, const Vec3& local, float radius, float amount) {
    if (amount <= 0.0f) return false;
    if (amount > 1.0f) amount = 1.0f;
    float gx = (local.x + f.HalfX()) / f.vs, gy = local.y / f.vs, gz = (local.z + f.HalfZ()) / f.vs, gr = radius / f.vs;
    int x0 = std::max(1, (int)std::floor(gx - gr)), x1 = std::min(f.nx - 2, (int)std::ceil(gx + gr));
    int y0 = std::max(std::max(1, f.floorLayers), (int)std::floor(gy - gr));
    int y1 = std::min(f.ny - 2, (int)std::ceil(gy + gr));
    int z0 = std::max(1, (int)std::floor(gz - gr)), z1 = std::min(f.nz - 2, (int)std::ceil(gz + gr));
    std::vector<float> src = f.d;   // the whole field, as the old code did
    bool changed = false;
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                float dx = x - gx, dy = y - gy, dz = z - gz;
                float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (d > gr) continue;
                float avg = (src[(std::size_t)f.Index(x - 1, y, z)] + src[(std::size_t)f.Index(x + 1, y, z)] +
                             src[(std::size_t)f.Index(x, y - 1, z)] + src[(std::size_t)f.Index(x, y + 1, z)] +
                             src[(std::size_t)f.Index(x, y, z - 1)] + src[(std::size_t)f.Index(x, y, z + 1)]) * (1.0f / 6.0f);
                float w = (1.0f - d / gr) * amount;
                float& cell = f.d[(std::size_t)f.Index(x, y, z)];
                float nv = RefClampTo(cell + (avg - cell) * w, f.Clamp());
                if (nv != cell) { cell = nv; changed = true; }
            }
    return changed;
}
bool RefBox(RefField& f, const Vec3& a, const Vec3& b, float amount) {
    Vec3 lo{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
    Vec3 hi{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
    int x0 = std::max(0, (int)std::floor((lo.x + f.HalfX()) / f.vs));
    int x1 = std::min(f.nx - 1, (int)std::ceil((hi.x + f.HalfX()) / f.vs));
    int y0 = std::max(0, (int)std::floor(lo.y / f.vs));
    int y1 = std::min(f.ny - 1, (int)std::ceil(hi.y / f.vs));
    int z0 = std::max(0, (int)std::floor((lo.z + f.HalfZ()) / f.vs));
    int z1 = std::min(f.nz - 1, (int)std::ceil((hi.z + f.HalfZ()) / f.vs));
    bool changed = false;
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                if (f.floorLayers > 0 && amount > 0.0f && y < f.floorLayers) continue;
                float& cell = f.d[(std::size_t)f.Index(x, y, z)];
                float nv = RefClampTo(cell - amount, f.Clamp());
                if (nv != cell) { cell = nv; changed = true; }
            }
    return changed;
}
// The old RemoveFloaters: one flood fill over the whole field.
int RefFloaters(RefField& f, int minVoxels) {
    if (minVoxels <= 1) return 0;
    const int n = f.Count();
    std::vector<char> visited((std::size_t)n, 0);
    std::vector<int> stack, comp;
    float air = f.iso - f.Clamp();
    int cleared = 0;
    for (int start = 0; start < n; ++start) {
        if (visited[(std::size_t)start] || f.d[(std::size_t)start] <= f.iso) continue;
        comp.clear(); stack.clear(); stack.push_back(start); visited[(std::size_t)start] = 1;
        while (!stack.empty()) {
            int idx = stack.back(); stack.pop_back();
            comp.push_back(idx);
            int x = idx % f.nx, y = (idx / f.nx) % f.ny, z = idx / (f.nx * f.ny);
            const int dxs[6] = {1, -1, 0, 0, 0, 0}, dys[6] = {0, 0, 1, -1, 0, 0}, dzs[6] = {0, 0, 0, 0, 1, -1};
            for (int k = 0; k < 6; ++k) {
                int xx = x + dxs[k], yy = y + dys[k], zz = z + dzs[k];
                if (!f.InBounds(xx, yy, zz)) continue;
                int ni = f.Index(xx, yy, zz);
                if (visited[(std::size_t)ni] || f.d[(std::size_t)ni] <= f.iso) continue;
                visited[(std::size_t)ni] = 1; stack.push_back(ni);
            }
        }
        if ((int)comp.size() < minVoxels) {
            for (int idx : comp) {
                int y = (idx / f.nx) % f.ny;
                if (f.floorLayers > 0 && y < f.floorLayers) continue;
                f.d[(std::size_t)idx] = air;
                ++cleared;
            }
        }
    }
    return cleared;
}

RefField RefOf(const VoxelTerrain& v) { return RefField{v.nx, v.ny, v.nz, v.voxelSize, v.iso, v.floorLayers, v.density}; }
bool SameDensity(const VoxelTerrain& v, const RefField& f) {
    return v.density.size() == f.d.size() && std::memcmp(v.density.data(), f.d.data(), f.d.size() * sizeof(float)) == 0;
}

GameObject* MakeTerrain(Scene& s, int X, int Y, int Z, float vox, float cave, unsigned seed, VoxelTerrain::RenderMode mode) {
    GameObject* go = s.CreateGameObject("Voxel");
    auto* v = go->AddComponent<VoxelTerrain>();
    v->Resize(X, Y, Z); v->voxelSize = vox;
    v->Generate(0.5f, 4.0f * vox, cave, seed);
    v->renderMode = mode;
    v->Apply();
    return go;
}

} // namespace

int main() {
    RUN_SUITE("voxel_chunks");
    VoxelTerrain::ForceOldRemesh(0);   // the tests below are about the new path; the old one has its own block at the end

    // --- 1. The fast marching-cubes loop is the original, bit for bit ---------------------------------------------
    {
        struct Cfg { int x, y, z; float vox, iso, cave; bool autoColor; };
        const Cfg cfgs[] = {
            {40, 26, 40, 1.0f, 0.0f, 0.0f, true},   {17, 9, 33, 0.8f, 0.0f, 0.5f, true},
            {34, 20, 19, 1.5f, 0.3f, 0.3f, true},   {5, 5, 5, 1.0f, 0.0f, 0.0f, false},
            {64, 36, 64, 0.8f, 0.0f, 0.2f, true},   {2, 2, 2, 1.0f, 0.0f, 0.0f, true},
            {31, 31, 31, 0.7f, -0.4f, 0.6f, false},
        };
        int ci = 0;
        for (const Cfg& c : cfgs) {
            VoxelTerrain v;
            v.Resize(c.x, c.y, c.z); v.voxelSize = c.vox; v.iso = c.iso; v.autoColor = c.autoColor;
            v.snowLevel = 0.6f * v.SizeY(); v.rockSlope = 0.35f;
            v.Generate(0.5f, 4.0f * c.vox, c.cave, 100u + (unsigned)ci);
            Lcg rng(77u + (unsigned)ci);
            for (int i = 0; i < 25; ++i) RandomEdit(v, rng, i);
            const Mesh a = v.BuildMesh(), b = v.BuildMeshReference();
            CHECK(a.TriangleCount() == b.TriangleCount());
            CHECK(IdenticalArrays(a, b));
            if (c.x >= 17) CHECK(a.TriangleCount() > 0);
            ++ci;
        }
    }

    // --- 2. The brushes still change the field exactly as before (for every ordinary input) ----------------------
    {
        VoxelTerrain v;
        v.Resize(36, 24, 30); v.voxelSize = 1.25f;
        v.Generate(0.5f, 5.0f, 0.4f, 9u);
        RefField f = RefOf(v);
        Lcg rng(4242u);
        int mismatches = 0, flagMismatches = 0;
        for (int i = 0; i < 120; ++i) {
            Vec3 c{rng.Range(-v.HalfX() - 3.0f, v.HalfX() + 3.0f), rng.Range(-2.0f, v.SizeY() + 2.0f), rng.Range(-v.HalfZ() - 3.0f, v.HalfZ() + 3.0f)};
            const float r = rng.Range(0.5f, 6.0f), amt = rng.Range(-4.0f, 4.0f);
            bool a, b;
            switch (i % 4) {
                case 0: a = v.Dig(c, r, amt); b = RefDig(f, c, r, amt); break;
                case 1: { const float st = rng.Range(0.1f, 1.5f); a = v.SmoothAt(c, r, st); b = RefSmooth(f, c, r, st); break; }
                case 2: a = v.Add(c, r, amt); b = RefDig(f, c, r, -amt); break;
                default: a = v.DigBox(c, {c.x + r, c.y + r, c.z + r}, amt); b = RefBox(f, c, {c.x + r, c.y + r, c.z + r}, amt); break;
            }
            if (a != b) ++flagMismatches;
            if (!SameDensity(v, f)) ++mismatches;
        }
        CHECK(mismatches == 0);
        CHECK(flagMismatches == 0);
    }

    // --- 3. The old and new floater cleanup clear the very same voxels --------------------------------------------
    {
        VoxelTerrain v;
        v.Resize(34, 30, 34); v.voxelSize = 1.0f; v.FillSlab(0.4f);
        Lcg rng(5u);
        // crumbs of many sizes floating above the slab, plus a hanging blob, then some digging
        for (int i = 0; i < 40; ++i) {
            const int cx = 3 + rng.Int(28), cy = 14 + rng.Int(12), cz = 3 + rng.Int(28), len = 1 + rng.Int(12);
            for (int k = 0; k < len; ++k) v.Set(cx + (k % 3 == 0 ? 1 : 0), cy + (k % 2), cz + k / 4, 4.0f);
        }
        v.Dig({0.0f, 6.0f, 0.0f}, 5.0f, 40.0f);
        RefField f = RefOf(v);
        const int cleared = v.RemoveFloaters(8);
        const int refCleared = RefFloaters(f, 8);
        CHECK(cleared == refCleared);
        CHECK(cleared > 0);
        CHECK(SameDensity(v, f));
        CHECK(v.RemoveFloaters(8) == 0);   // nothing left to clear
    }

    // --- 4. RemoveFloatersEdited: the first call sweeps everything; later calls look only where the edits were -----
    {
        VoxelTerrain v;
        v.Resize(40, 30, 40); v.voxelSize = 1.0f; v.FillSlab(0.35f);
        // a crumb that was there from the start, far from where we dig
        v.Set(35, 20, 35, 4.0f); v.Set(35, 21, 35, 4.0f);
        // a bump on the slab, joined to it by a single column of voxels
        for (int y = 9; y < 14; ++y) v.Set(20, y, 20, 4.0f);
        for (int x = 19; x <= 21; ++x) for (int z = 19; z <= 21; ++z) v.Set(x, 14, z, 4.0f);
        CHECK(v.RemoveFloatersEdited(8) >= 2);                    // first call: the whole field, the far crumb goes
        CHECK(!v.SolidAt({35 - v.HalfX(), 20.0f, 35 - v.HalfZ()}));
        CHECK(v.SolidAt({20 - v.HalfX(), 14.0f, 20 - v.HalfZ()}));  // the bump is still attached
        // cut the column: the bump is now a floating piece of 9 + voxels, make it small enough to count as a crumb
        for (int y = 9; y < 14; ++y) v.Set(20, y, 20, -3.0f);
        for (int x = 19; x <= 21; ++x) for (int z = 19; z <= 21; ++z) v.Set(x, 14, z, 4.0f);   // 9 voxels: not a crumb (>= 8)
        CHECK(v.RemoveFloatersEdited(8) == 0);
        v.Set(19, 14, 19, -3.0f); v.Set(21, 14, 21, -3.0f);       // now 7 voxels
        const int n = v.RemoveFloatersEdited(8);
        CHECK(n == 7);
        CHECK(!v.SolidAt({20 - v.HalfX(), 14.0f, 20 - v.HalfZ()}));
        // a crumb planted behind the cleanup's back (raw write, never reported as an edit) is left alone by the local
        // pass and caught by the full one
        v.density[(std::size_t)v.Index(5, 25, 5)] = 4.0f;
        CHECK(v.RemoveFloatersEdited(8) == 0);
        CHECK(v.SolidAt({5 - v.HalfX(), 25.0f, 5 - v.HalfZ()}));
        CHECK(v.RemoveFloaters(8) == 1);
        // a long thin piece (15 voxels) of which only the first voxel is a reported edit: the local walk starts there, must
        // not be cut short by the edge of the region it may visit, and must leave the piece alone (it is big)
        for (int k = 1; k < 15; ++k) v.density[(std::size_t)v.Index(10, 22, 10 + k)] = 4.0f;   // raw writes: not edits
        v.Set(10, 22, 10, 4.0f);
        CHECK(v.RemoveFloatersEdited(8) == 0);
        CHECK(v.SolidAt({10 - v.HalfX(), 22.0f, 24 - v.HalfZ()}));
        // the floor is never cleared
        VoxelTerrain w;
        w.Resize(8, 8, 8); w.FillSlab(0.3f);
        const int before = (int)std::count_if(w.density.begin(), w.density.end(), [&](float d) { return d > w.iso; });
        w.RemoveFloaters(100000);   // everything is "small" now: only the floor row is protected
        int after = 0;
        for (int z = 0; z < w.nz; ++z) for (int x = 0; x < w.nx; ++x) if (w.density[(std::size_t)w.Index(x, 0, z)] > w.iso) ++after;
        CHECK(after == w.nx * w.nz);
        CHECK(before > after);
    }

    // --- 4b. After any edit, the local cleanup leaves the field exactly as the whole-field cleanup does ---------------------
    {
        VoxelTerrain a;
        a.Resize(38, 26, 38); a.voxelSize = 1.0f;
        a.Generate(0.5f, 5.0f, 0.55f, 21u);          // hills and caves: plenty of places for a dig to cut a piece loose
        a.RemoveFloatersEdited(8);                   // the first call is the whole-field sweep
        VoxelTerrain b = a;                          // a copy of the field (components copy by value in tests)
        Lcg rng(808u);
        int roundsWithCrumbs = 0, mismatches = 0;
        for (int i = 0; i < 40; ++i) {
            Vec3 c{rng.Range(-a.HalfX(), a.HalfX()), rng.Range(2.0f, 16.0f), rng.Range(-a.HalfZ(), a.HalfZ())};
            const float r = rng.Range(1.5f, 4.5f);
            if (i % 2 == 0) {                        // a small blob of rock dropped into the open air: a floating crumb
                for (int tries = 0; tries < 80 && a.SampleDensity(c) > -2.0f; ++tries)
                    c = {rng.Range(-a.HalfX(), a.HalfX()), rng.Range(4.0f, 22.0f), rng.Range(-a.HalfZ(), a.HalfZ())};
                a.Add(c, rng.Range(0.9f, 1.4f), 8.0f);
            } else if (i % 4 == 1) a.DigBox({c.x - r, c.y - r, c.z - r}, {c.x + r, c.y + r, c.z + r}, rng.Range(1.0f, 6.0f));
            else a.Dig(c, r, rng.Range(2.0f, 8.0f));
            b.density = a.density;                   // same field, but b has never been told what was edited
            const int na = a.RemoveFloatersEdited(8);
            const int nb = b.RemoveFloaters(8);
            if (nb > 0) ++roundsWithCrumbs;
            if (na != nb || a.density != b.density) ++mismatches;
        }
        CHECK(roundsWithCrumbs > 3);   // the digs really did cut pieces loose
        CHECK(mismatches == 0);
    }

    // --- 5. Exactly the chunks an edit reaches are meshed again ---------------------------------------------------
    {
        Scene s("DIRTY"); s.physicsEnabled = false;
        GameObject* go = MakeTerrain(s, 72, 44, 72, 1.5f, 0.0f, 2024u, VoxelTerrain::RenderMode::Single);
        auto* v = go->GetComponent<VoxelTerrain>();
        CHECK(v->lastStats.chunksRemeshed == v->lastStats.chunksTotal);      // the first build does everything
        v->Apply();
        CHECK(v->lastStats.chunksRemeshed == 0);                             // nothing changed: nothing is meshed
        // an edit far from every chunk border touches one chunk; the neighbours are not meshed
        Vec3 mid{-v->HalfX() + 8.0f * 1.5f, 0.0f, -v->HalfZ() + 8.0f * 1.5f};
        float sy = 0.0f; v->SurfaceY(mid.x, mid.z, sy);
        mid.y = sy;
        CHECK(v->Dig(mid, 1.5f, 3.0f));
        v->Apply();
        CHECK(v->lastStats.chunksRemeshed >= 1);
        CHECK(v->lastStats.chunksRemeshed <= 8);
        CHECK(Differences(GatherDrawn(go), v->BuildMesh()) == 0);
        // a brush that changes nothing marks nothing
        CHECK(!v->Dig({0, v->SizeY() + 50.0f, 0}, 3.0f, 3.0f));
        v->Apply();
        CHECK(v->lastStats.chunksRemeshed == 0);
        // changing a colour re-meshes everything (the face colours are part of the mesh)
        v->snowLevel += 1.0f;
        v->Apply();
        CHECK(v->lastStats.chunksRemeshed == v->lastStats.chunksTotal);
        CHECK(Differences(GatherDrawn(go), v->BuildMesh()) == 0);
    }

    // --- 6. A single cell written anywhere (borders, corners, edges) leaves the mesh equal to a full rebuild ---------
    {
        Scene s("BORDER"); s.physicsEnabled = false;
        GameObject* go = MakeTerrain(s, 36, 20, 36, 1.0f, 0.3f, 31u, VoxelTerrain::RenderMode::Single);
        auto* v = go->GetComponent<VoxelTerrain>();
        const int xs[] = {0, 1, 2, 14, 15, 16, 17, 18, 31, 32, 33, 34, 35}, ys[] = {1, 2, 6, 10, 14, 15, 16, 17, 18, 19}, zs[] = {0, 1, 15, 16, 17, 34, 35};
        int bad = 0, n = 0;
        Lcg rng(99u);
        for (int x : xs) for (int y : ys) for (int z : zs) {
            if ((x * 7 + y * 3 + z) % 5 != 0) continue;     // a spread of them, not all 910
            v->Set(x, y, z, rng.Range(-2.5f, 2.5f));
            v->Apply();
            ++n;
            if (Differences(GatherDrawn(go), v->BuildMesh()) != 0) ++bad;
        }
        CHECK(n > 40);
        CHECK(bad == 0);
    }

    // --- 7. The three ways to hand the surface to the renderer all draw the same triangles ------------------------
    {
        const VoxelTerrain::RenderMode modes[] = {VoxelTerrain::RenderMode::Single, VoxelTerrain::RenderMode::Chunked, VoxelTerrain::RenderMode::Auto};
        const char* names[] = {"single", "chunked", "auto"};
        for (int mi = 0; mi < 3; ++mi) {
            Scene s("MODES"); s.physicsEnabled = false;
            GameObject* go = MakeTerrain(s, 40, 26, 40, 1.0f, 0.2f, 8u, modes[mi]);
            auto* v = go->GetComponent<VoxelTerrain>();
            auto* mr = go->GetComponent<MeshRenderer>();
            if (mi == 0) { CHECK(mr->mesh.vertices.size() > 0); CHECK(v->ChunkObjectCount() == 0); }
            if (mi == 1) { CHECK(mr->mesh.vertices.empty()); CHECK(v->ChunkObjectCount() > 0); CHECK(v->UsesChunkObjects()); }
            if (mi == 2) { CHECK(mr->mesh.vertices.size() > 0); CHECK(v->ChunkObjectCount() == 0); }   // not edited yet: the combined mesh
            if (mi == 2) {   // the first real edit of a running game switches over
                Vec3 c{0.0f, 0.0f, 0.0f}; float sy = 0.0f; v->SurfaceY(0, 0, sy); c.y = sy;
                CHECK(v->Dig(c, 2.5f, 3.0f));
                v->Apply();
                CHECK(v->UsesChunkObjects()); CHECK(mr->mesh.vertices.empty()); CHECK(v->ChunkObjectCount() > 0);
            }
            Lcg rng(55u);
            int bad = 0;
            for (int i = 0; i < 30; ++i) {
                RandomEdit(*v, rng, i);
                v->Apply();
                if (Differences(GatherDrawn(go), v->BuildMesh()) != 0) ++bad;
            }
            if (bad) std::cout << "  mode " << names[mi] << ": " << bad << " steps differ\n";
            CHECK(bad == 0);
            if (mi == 0) { CHECK(v->ChunkObjectCount() == 0); CHECK(!v->UsesChunkObjects()); }
        }
        // a game that is not running (the editor's Edit mode) keeps the combined mesh even when edited
        Scene s("EDITMODE"); s.physicsEnabled = false; s.runScripts = false;
        GameObject* go = MakeTerrain(s, 40, 26, 40, 1.0f, 0.0f, 8u, VoxelTerrain::RenderMode::Auto);
        auto* v = go->GetComponent<VoxelTerrain>();
        Lcg rng(1u);
        for (int i = 0; i < 6; ++i) { RandomEdit(*v, rng, i); v->Apply(); }
        CHECK(!v->UsesChunkObjects());
        CHECK(go->GetComponent<MeshRenderer>()->mesh.vertices.size() > 0);
        CHECK(Differences(GatherDrawn(go), v->BuildMesh()) == 0);
    }

    // a terrain whose own mesh is read by a collider (or an LOD group, or an occluder) stays on the combined mesh in Auto mode
    {
        Scene s("COL"); s.physicsEnabled = false;
        GameObject* go = MakeTerrain(s, 40, 26, 40, 1.0f, 0.0f, 8u, VoxelTerrain::RenderMode::Auto);
        go->AddComponent<BoxCollider3D>();
        auto* v = go->GetComponent<VoxelTerrain>();
        Vec3 c{0.0f, 0.0f, 0.0f}; float sy = 0.0f; v->SurfaceY(0, 0, sy); c.y = sy;
        CHECK(v->Dig(c, 2.5f, 3.0f));
        v->Apply();
        CHECK(!v->UsesChunkObjects());
        CHECK(go->GetComponent<MeshRenderer>()->mesh.vertices.size() > 0);
        CHECK(Differences(GatherDrawn(go), v->BuildMesh()) == 0);
        // asked for by name, chunks are used anyway
        v->renderMode = VoxelTerrain::RenderMode::Chunked;
        v->Dig(c, 2.5f, 3.0f); v->Apply();
        CHECK(v->UsesChunkObjects());
        CHECK(Differences(GatherDrawn(go), v->BuildMesh()) == 0);
    }

    // --- 8. What a chunk object is -------------------------------------------------------------------------------
    {
        Scene s("OBJ"); s.physicsEnabled = false;
        GameObject* go = MakeTerrain(s, 40, 26, 40, 1.0f, 0.0f, 3u, VoxelTerrain::RenderMode::Chunked);
        go->layer = 7;
        go->transform->localPosition = {10, 5, -3};
        auto* v = go->GetComponent<VoxelTerrain>();
        v->Apply();
        std::vector<GameObject*> chunks = ChunkObjects(go);
        CHECK(!chunks.empty());
        CHECK((int)chunks.size() == v->ChunkObjectCount());
        bool allOk = true;
        for (GameObject* c : chunks) {
            auto* cm = c->GetComponent<MeshRenderer>();
            if (!c->transient || c->transform->Parent() != go->transform) allOk = false;
            if (!cm || cm->mesh.vertices.empty() || !cm->mesh.HasNormals() || !cm->mesh.HasFaceColors()) allOk = false;
            if (c->transform->localPosition.x != 0 || c->transform->localPosition.y != 0 || c->transform->localPosition.z != 0) allOk = false;
            if (c->transform->localScale.x != 1.0f || c->name.rfind("Voxel Chunk ", 0) != 0) allOk = false;
            if (c->GetComponent<Rigidbody3D>() || c->GetComponent<Collider3D>()) allOk = false;
        }
        CHECK(allOk);
        // the chunks are where the terrain is, even when the terrain moves
        const Vec3 wp = chunks[0]->transform->LocalToWorldMatrix().MultiplyPoint({0, 0, 0});
        CHECK_NEAR(wp.x, 10.0f, 1e-4f); CHECK_NEAR(wp.y, 5.0f, 1e-4f); CHECK_NEAR(wp.z, -3.0f, 1e-4f);
        // a scene file never gets them
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("Voxel Chunk") == std::string::npos);
        CHECK(text.find("voxelterrain") != std::string::npos);
        CHECK(text.find("meshcolors") == std::string::npos);      // the terrain rebuilds its colours; saving them only bloated the file
        // a chunk with no surface in it gets no object (the grid is 3 x 2 x 3 chunks; the sky chunks stay empty)
        CHECK(v->ChunkObjectCount() < 3 * 2 * 3);
    }

    // --- 9. The chunks copy the terrain's renderer settings and follow them ----------------------------------------
    {
        Scene s("MAT"); s.physicsEnabled = false;
        GameObject* go = MakeTerrain(s, 32, 20, 32, 1.0f, 0.0f, 3u, VoxelTerrain::RenderMode::Chunked);
        auto* mr = go->GetComponent<MeshRenderer>();
        auto* v = go->GetComponent<VoxelTerrain>();
        mr->shader = MeshRenderer::Shader::Toon; mr->toonBands = 4; mr->castShadows = false; mr->receiveShadows = false;
        mr->specular = 0.5f; mr->shininess = 40.0f; mr->emissive = Color(0.1f, 0.2f, 0.3f, 1.0f);
        mr->reflectivity = 0.25f; mr->metallic = 0.6f; mr->normalMap = "n.png"; mr->uvScroll = {0.5f, 0.25f};
        mr->rimStrength = 0.4f; mr->wireframe = false; mr->outline = true; mr->unlit = false;
        mr->texFilter = MeshRenderer::TexFilter::Pixel; mr->aoMap = "ao.png"; mr->matcap = "mat";
        v->texture = "rock.png"; v->textureTiling = 0.07f;
        v->Apply();
        s.Update(0.016f);   // Update copies anything that changed
        auto chunkOk = [&](const MeshRenderer& c) {
            return c.shader == MeshRenderer::Shader::Toon && c.toonBands == 4 && !c.castShadows && !c.receiveShadows &&
                   c.specular == 0.5f && c.shininess == 40.0f && c.emissive.g == 0.2f && c.reflectivity == 0.25f &&
                   c.metallic == 0.6f && c.normalMap == "n.png" && c.uvScroll.x == 0.5f && c.rimStrength == 0.4f &&
                   c.outline && c.texFilter == MeshRenderer::TexFilter::Pixel && c.aoMap == "ao.png" && c.matcap == "mat" &&
                   c.texture == "rock.png" && c.triplanar && c.tiling.x == 0.07f && !c.doubleSided && c.enabled;
        };
        bool allOk = true;
        for (GameObject* c : ChunkObjects(go)) allOk = allOk && chunkOk(*c->GetComponent<MeshRenderer>());
        CHECK(allOk);
        CHECK(!ChunkObjects(go).empty());
        // a later change on the terrain's own renderer reaches the chunks on the next frame, no edit needed
        mr->metallic = 0.9f; mr->castShadows = true; mr->enabled = false; mr->color = Color(0.2f, 0.4f, 0.6f, 1.0f);
        s.Update(0.016f);
        bool follow = true;
        for (GameObject* c : ChunkObjects(go)) {
            auto* cm = c->GetComponent<MeshRenderer>();
            follow = follow && cm->metallic == 0.9f && cm->castShadows && !cm->enabled && cm->color.g == 0.4f;
        }
        CHECK(follow);
        // the colour the terrain sets itself comes through too
        v->color = Color(0.9f, 0.1f, 0.1f, 1.0f); v->Apply();
        for (GameObject* c : ChunkObjects(go)) CHECK(c->GetComponent<MeshRenderer>()->color.r == 0.9f);
        // a chunk that gets its first surface later (here a floating block high above the hills) starts with the same settings
        const int countBefore = v->ChunkObjectCount();
        for (int x = 18; x <= 21; ++x) for (int y = 16; y <= 18; ++y) for (int z = 18; z <= 21; ++z) v->Set(x, y, z, 3.0f);
        v->Apply();
        CHECK(v->ChunkObjectCount() > countBefore);
        for (GameObject* c : ChunkObjects(go)) CHECK(c->GetComponent<MeshRenderer>()->metallic == 0.9f);
    }

    // --- 10. Chunk objects live and die with the terrain ----------------------------------------------------------
    {
        Scene s("LIFE"); s.physicsEnabled = false;
        const std::size_t before = s.Objects().size();
        GameObject* go = MakeTerrain(s, 40, 26, 40, 1.0f, 0.0f, 3u, VoxelTerrain::RenderMode::Chunked);
        auto* v = go->GetComponent<VoxelTerrain>();
        const int n = v->ChunkObjectCount();
        CHECK(n > 0);
        CHECK(s.Objects().size() == before + 1 + (std::size_t)n);
        // a resize drops the old chunks (hidden at once) and builds the new ones
        v->Resize(24, 18, 24); v->Generate(0.5f, 3.0f, 0.0f, 4u); v->Apply();
        int visible = 0;
        for (const auto& up : s.Objects()) if (up->transient && up->active && !up->IsDestroyed() && up->GetComponent<MeshRenderer>()) ++visible;
        CHECK(visible == v->ChunkObjectCount());
        s.Update(0.016f);   // the old ones are freed at the end of the frame
        CHECK(s.Objects().size() == before + 1 + (std::size_t)v->ChunkObjectCount());
        // removing the component takes its chunks with it
        go->RemoveComponent(v);
        s.Update(0.016f);
        CHECK(s.Objects().size() == before + 1);
        // so does destroying the object
        GameObject* g2 = MakeTerrain(s, 40, 26, 40, 1.0f, 0.0f, 3u, VoxelTerrain::RenderMode::Chunked);
        CHECK(s.Objects().size() > before + 2);
        s.Destroy(g2);
        s.Update(0.016f);
        CHECK(s.Objects().size() == before + 1);
    }

    // --- 11. The picture is the same: the software renderer draws single and chunked terrain to the same pixels ----
    {
        const Vec3 eye{0.0f, 26.0f, 38.0f};
        const Mat4 vp = Mat4::Perspective(60.0f, 4.0f / 3.0f, 0.1f, 300.0f) * Mat4::LookAt(eye, Vec3{0, 6, 0}, Vec3{0, 1, 0});
        auto frame = [&](VoxelTerrain::RenderMode mode, Raster& r, bool texture) {
            Scene s("PIX"); s.physicsEnabled = false;
            GameObject* sun = s.CreateGameObject("Sun");
            sun->transform->localRotation = Quat::LookRotation(Vec3{0.3f, -1, 0.2f}.Normalized(), Vec3{0, 0, 1});
            sun->AddComponent<Light>()->type = Light::Type::Directional;
            GameObject* go = MakeTerrain(s, 44, 26, 44, 1.0f, 0.3f, 12u, mode);
            auto* v = go->GetComponent<VoxelTerrain>();
            if (texture) { v->texture = ""; }
            Lcg rng(321u);
            for (int i = 0; i < 12; ++i) { RandomEdit(*v, rng, i); v->Apply(); }
            ApplySceneLight(s);
            r.Resize(200, 150); r.Clear(0xFF101820u);
            RenderMeshes(r, s, vp, eye);
        };
        Raster single, chunked;
        frame(VoxelTerrain::RenderMode::Single, single, false);
        frame(VoxelTerrain::RenderMode::Chunked, chunked, false);
        int lit = 0, diff = 0;
        for (std::size_t i = 0; i < single.color.size(); ++i) {
            if (single.color[i] != 0xFF101820u) ++lit;
            if (single.color[i] != chunked.color[i]) ++diff;
        }
        CHECK(lit > 5000);        // the terrain really is in the picture
        CHECK(diff == 0);         // and is the same picture
        if (diff) std::cout << "  pixels that differ: " << diff << " of " << single.color.size() << "\n";
    }

    // --- 12. A dig in the running game hands over only the chunks it touched -------------------------------------
    {
        Scene sa("BYTES-A"), sb("BYTES-B"); sa.physicsEnabled = false; sb.physicsEnabled = false;
        GameObject* ga = MakeTerrain(sa, 72, 44, 72, 1.5f, 0.0f, 2024u, VoxelTerrain::RenderMode::Single);
        GameObject* gb = MakeTerrain(sb, 72, 44, 72, 1.5f, 0.0f, 2024u, VoxelTerrain::RenderMode::Chunked);
        auto* va = ga->GetComponent<VoxelTerrain>(); auto* vb = gb->GetComponent<VoxelTerrain>();
        Vec3 c{3.0f, 0.0f, -4.0f}; float sy = 0.0f; va->SurfaceY(c.x, c.z, sy); c.y = sy;
        va->Dig(c, 3.0f, 2.0f); vb->Dig(c, 3.0f, 2.0f);
        va->Apply(); vb->Apply();
        const std::size_t whole = va->lastStats.bytesHandedOff, part = vb->lastStats.bytesHandedOff;
        std::cout << "  one dig frame hands the renderer " << whole << " bytes (single) and " << part << " bytes (chunked)\n";
        CHECK(whole > 1000000);
        CHECK(part > 0);
        CHECK(part * 6 < whole);
        CHECK(vb->lastStats.chunksHandedOff >= 1 && vb->lastStats.chunksHandedOff <= 8);
        CHECK(vb->lastStats.chunked && !va->lastStats.chunked);
    }

    // --- 13. The old path comes back with one switch ---------------------------------------------------------------
    {
        VoxelTerrain::ForceOldRemesh(1);
        CHECK(VoxelTerrain::OldRemeshActive());
        Scene s("OLD"); s.physicsEnabled = false;
        GameObject* go = MakeTerrain(s, 40, 26, 40, 1.0f, 0.2f, 8u, VoxelTerrain::RenderMode::Chunked);   // the switch wins over the mode
        auto* v = go->GetComponent<VoxelTerrain>();
        CHECK(!v->UsesChunkObjects());
        CHECK(v->ChunkObjectCount() == 0);
        Lcg rng(66u);
        int bad = 0;
        for (int i = 0; i < 15; ++i) {
            RandomEdit(*v, rng, i);
            v->Apply();
            const Mesh& m = go->GetComponent<MeshRenderer>()->mesh;
            if (Differences(m, v->BuildMeshReference()) != 0) ++bad;
            if (v->lastStats.chunked) bad += 100;
        }
        CHECK(bad == 0);
        // Apply with nothing to do still rebuilds the combined mesh, as it always did
        v->Apply();
        CHECK(v->lastStats.handoffMs >= 0.0);
        VoxelTerrain::ForceOldRemesh(0);
        CHECK(!VoxelTerrain::OldRemeshActive());
        // and the same scene switches over to the new path at the next edit
        v->renderMode = VoxelTerrain::RenderMode::Chunked;
        RandomEdit(*v, rng, 1); v->Apply();
        CHECK(v->UsesChunkObjects());
        CHECK(Differences(GatherDrawn(go), v->BuildMesh()) == 0);
        // ... and back to one mesh
        v->renderMode = VoxelTerrain::RenderMode::Single;
        v->Apply();
        CHECK(!v->UsesChunkObjects());
        s.Update(0.016f);
        CHECK(v->ChunkObjectCount() == 0);
        CHECK(ChunkObjects(go).empty());
        CHECK(Differences(GatherDrawn(go), v->BuildMesh()) == 0);
    }

    // --- 14. Saved scenes are unchanged: the density text does not depend on how the surface is drawn -------------
    {
        Scene sa("SAVE-A"), sb("SAVE-B"); sa.physicsEnabled = false; sb.physicsEnabled = false;
        GameObject* ga = MakeTerrain(sa, 24, 18, 24, 1.0f, 0.3f, 6u, VoxelTerrain::RenderMode::Single);
        GameObject* gb = MakeTerrain(sb, 24, 18, 24, 1.0f, 0.3f, 6u, VoxelTerrain::RenderMode::Chunked);
        Lcg r1(9u), r2(9u);
        for (int i = 0; i < 10; ++i) {
            RandomEdit(*ga->GetComponent<VoxelTerrain>(), r1, i); ga->GetComponent<VoxelTerrain>()->Apply();
            RandomEdit(*gb->GetComponent<VoxelTerrain>(), r2, i); gb->GetComponent<VoxelTerrain>()->Apply();
        }
        CHECK(ga->GetComponent<VoxelTerrain>()->EncodeDensity() == gb->GetComponent<VoxelTerrain>()->EncodeDensity());
    }

    TEST_MAIN_RESULT();
}
