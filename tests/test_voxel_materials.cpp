#include "test_framework.hpp"
#include "voxel_mesh_util.hpp"
#include <cmath>
#include <iostream>

using namespace okay;
using namespace voxeltest;

// Materials per voxel and real-life digging: nothing a brush takes away is lost, hard ground digs slower, shapes, bedrock,
// depth limit, undo of a stroke, slumping, loose pieces, saving, and the Voxel Digger's store, debris and presets.
namespace {
VoxelTerrain MakeField(int X = 36, int Y = 30, int Z = 36) {
    VoxelTerrain v;
    v.Resize(X, Y, Z); v.voxelSize = 1.0f; v.FillSlab(0.5f);
    v.EnableMaterials(0);
    return v;
}
double Sum(const std::vector<double>& a) { double t = 0; for (double x : a) t += x; return t; }
}

int main() {
    RUN_SUITE("voxel_materials");
    VoxelTerrain::ForceOldRemesh(0);

    // --- materials: table, generation by layers, colours --------------------------------------------------------------
    {
        VoxelTerrain v;
        v.Resize(40, 36, 40); v.voxelSize = 1.0f; v.Generate(0.5f, 6.0f, 0.0f, 3u);
        CHECK(!v.UseMaterials());
        v.GenerateMaterials(3u);
        CHECK(v.UseMaterials());
        CHECK(v.materials.size() == 7 && v.MaterialIndex("Stone") == 4 && v.MaterialIndex("Nope") == -1);
        int count[7] = {0};
        for (std::size_t i = 0; i < v.material.size(); ++i) if (v.density[i] > v.iso) ++count[v.material[i]];
        CHECK(count[1] > 100);            // grass on top
        CHECK(count[0] > 100);            // dirt under it
        CHECK(count[4] > count[0]);       // stone is the bulk
        CHECK(count[5] > 5);              // ore pockets
        // the top voxel of a gentle column is grass, deep ones are stone
        float sy = 0; v.SurfaceY(0, 0, sy);
        const int gx = v.nx / 2, gz = v.nz / 2;
        int topY = 0; for (int y = v.ny - 1; y >= 0; --y) if (v.Get(gx, y, gz) > v.iso) { topY = y; break; }
        CHECK(v.MaterialAt(gx, 2, gz) == 4);
        CHECK(v.MaterialAt(gx, topY, gz) != 4 || topY > 0);
        // the mesh takes the material colours
        const Mesh m = v.BuildMesh();
        CHECK(m.HasFaceColors());
        std::size_t matching = 0;
        for (const Color& c : m.triColors) for (const auto& mat : v.materials) if (c.r == mat.color.r && c.g == mat.color.g && c.b == mat.color.b) { ++matching; break; }
        CHECK(matching == m.triColors.size());
        v.DisableMaterials();
        CHECK(!v.UseMaterials());
    }

    // --- Edit with default options is exactly Dig / Add ------------------------------------------------------------------
    {
        VoxelTerrain a = MakeField(), b = MakeField();
        a.DisableMaterials(); b.DisableMaterials();
        Lcg rng(5u);
        bool same = true;
        for (int i = 0; i < 25; ++i) {
            Vec3 c{rng.Range(-14, 14), rng.Range(4, 20), rng.Range(-14, 14)};
            const float r = rng.Range(1.0f, 4.0f), amt = rng.Range(-4.0f, 4.0f);
            if (amt >= 0) a.Dig(c, r, amt); else a.Add(c, r, -amt);
            b.Edit(c, r, amt, VoxelTerrain::EditOptions{});
            same = same && a.density == b.density;
        }
        CHECK(same);
    }

    // --- nothing disappears: the volume taken out is counted exactly, per material -----------------------------------------
    {
        VoxelTerrain v = MakeField();
        const int stone = v.MaterialIndex("Stone"), sand = v.MaterialIndex("Sand");
        // a sand layer on top of stone
        for (int z = 0; z < v.nz; ++z) for (int y = 0; y < v.ny; ++y) for (int x = 0; x < v.nx; ++x)
            v.material[(std::size_t)v.Index(x, y, z)] = (unsigned char)(y >= 12 ? sand : stone);
        const double before = v.SolidVolume();
        VoxelTerrain::EditOptions o;
        double removed = 0, added = 0; std::vector<double> by(7, 0.0);
        for (int i = 0; i < 12; ++i) {
            const auto r = v.Edit({0.0f, 14.0f - i * 0.4f, 0.0f}, 3.0f, 3.0f, o);
            removed += r.removed; added += r.added;
            for (std::size_t k = 0; k < r.removedBy.size(); ++k) by[k] += r.removedBy[k];
        }
        const double after = v.SolidVolume();
        std::cout << "  removed " << removed << " m3, solid volume " << before << " -> " << after << "\n";
        CHECK(removed > 5.0);
        CHECK_NEAR(before - after, removed - added, 1e-6 * before);          // the books balance exactly
        CHECK_NEAR(Sum(by), removed, 1e-9);
        CHECK(by[(std::size_t)sand] > 0.5 && by[(std::size_t)stone] > 0.5);  // both layers gave material
        // building it back from the same amount restores the shape
        const double put = v.AddVolume({0.0f, 12.0f, 0.0f}, removed, sand, 2.5f);
        CHECK(put >= removed * 0.98);
        const double restored = v.SolidVolume();
        std::cout << "  put back " << put << " m3, solid volume now " << restored << "\n";
        CHECK_NEAR(restored, before, before * 0.02);
        CHECK(AllFinite(v));
    }

    // --- hardness and tool power -----------------------------------------------------------------------------------------
    {
        auto drop = [](int mat, float power) {
            VoxelTerrain v = MakeField();
            for (auto& m : v.material) m = (unsigned char)mat;
            VoxelTerrain::EditOptions o; o.power = power;
            double removed = 0;
            for (int i = 0; i < 2; ++i) removed += v.Edit({0.0f, 14.5f, 0.0f}, 3.0f, 1.0f, o).removed;
            return removed;
        };
        const double dirt = drop(0, 1.0e9f), stone = drop(4, 1.0e9f), ore = drop(5, 1.0e9f);
        std::cout << "  same 2 strokes remove dirt " << dirt << ", stone " << stone << ", ore " << ore << " m3\n";
        CHECK(dirt > stone * 2.5);                  // stone (hardness 4) is much slower than dirt
        CHECK(stone > ore);
        CHECK(drop(4, 3.0f) == 0.0);                // a power-3 tool cannot dig stone (4)
        CHECK(drop(0, 3.0f) > 0.0);                 // but digs dirt
        VoxelTerrain s = MakeField();
        VoxelTerrain::EditOptions slow; slow.hardnessScale = 3.0f;
        VoxelTerrain f = MakeField();
        CHECK(f.Edit({0, 14.5f, 0}, 3, 2, VoxelTerrain::EditOptions{}).removed > s.Edit({0, 14.5f, 0}, 3, 2, slow).removed * 1.5);
    }

    // --- brush shapes, snap, bedrock, depth limit ------------------------------------------------------------------------
    {
        auto vol = [](VoxelTerrain::EditOptions::Shape sh) {
            VoxelTerrain v = MakeField();
            VoxelTerrain::EditOptions o; o.shape = sh;
            double r = 0; for (int i = 0; i < 4; ++i) r += v.Edit({0.0f, 14.5f, 0.0f}, 3.0f, 3.0f, o).removed;
            return r;
        };
        const double sph = vol(VoxelTerrain::EditOptions::Shape::Sphere), cube = vol(VoxelTerrain::EditOptions::Shape::Cube),
                     cyl = vol(VoxelTerrain::EditOptions::Shape::Cylinder), pl = vol(VoxelTerrain::EditOptions::Shape::Plane);
        std::cout << "  volumes: sphere " << sph << " cube " << cube << " cylinder " << cyl << " plane " << pl << "\n";
        CHECK(cube > sph * 1.2);
        CHECK(cyl > sph);
        CHECK(pl < cyl);
        // bedrock: nothing under the line moves, even with a huge brush
        VoxelTerrain b = MakeField();
        const std::vector<float> before = b.density;
        VoxelTerrain::EditOptions o; o.protectBelowY = 10.0f;
        b.Edit({0.0f, 6.0f, 0.0f}, 8.0f, 20.0f, o);
        bool low = true;
        for (int y = 0; y < 10; ++y) for (int i = 0; i < b.nx * b.nz; ++i) {
            const int x = i % b.nx, z = i / b.nx;
            if (b.density[(std::size_t)b.Index(x, y, z)] != before[(std::size_t)b.Index(x, y, z)]) low = false;
        }
        CHECK(low);
        // depth limit: a stroke that began at y = 14 may not go below 14 - 3
        VoxelTerrain d = MakeField();
        VoxelTerrain::EditOptions dl; dl.maxDepth = 3.0f; dl.depthOriginY = 14.0f;
        for (int i = 0; i < 10; ++i) d.Edit({0.0f, 8.0f, 0.0f}, 6.0f, 6.0f, dl);
        bool deep = false;
        for (int y = 0; y < 11; ++y) if (d.Get(d.nx / 2, y, d.nz / 2) != MakeField().Get(d.nx / 2, y, d.nz / 2)) deep = true;
        CHECK(!deep);
        // snap: two centres inside the same voxel give the same result
        VoxelTerrain s1 = MakeField(), s2 = MakeField();
        VoxelTerrain::EditOptions sn; sn.snap = true;
        s1.Edit({0.0f, 14.3f, 0.0f}, 2.5f, 3.0f, sn); s2.Edit({0.2f, 14.45f, 0.2f}, 2.5f, 3.0f, sn);
        CHECK(s1.density == s2.density);
    }

    // --- undo of a stroke --------------------------------------------------------------------------------------------------
    {
        VoxelTerrain v = MakeField();
        const std::vector<float> d0 = v.density; const std::vector<unsigned char> m0 = v.material;
        CHECK(!v.CanUndo());
        v.BeginStroke();
        VoxelTerrain::EditOptions o; o.addMaterial = 2;
        for (int i = 0; i < 8; ++i) { v.Edit({1.0f * i - 4, 14.5f, 0.0f}, 2.5f, 3.0f, o); v.Edit({0, 18.5f, 0.0f}, 2.0f, -3.0f, o); }
        v.EndStroke();
        CHECK(v.density != d0 && v.CanUndo());
        CHECK(v.UndoStroke());
        CHECK(v.density == d0 && v.material == m0);
        CHECK(!v.CanUndo() && !v.UndoStroke());
    }

    // --- slumping: loose ground slides down, the volume stays ---------------------------------------------------------------
    {
        VoxelTerrain v = MakeField();
        for (auto& m : v.material) m = 2;                       // sand
        // carve a trench under the middle so a sand shelf hangs over air
        v.Edit({0, 14, 0}, 4.0f, 30.0f, VoxelTerrain::EditOptions{});
        const double before = v.SolidVolume();
        int total = 0, worst = 0;
        for (int i = 0; i < 20; ++i) { const int n = v.Slump(15); worst = std::max(worst, n); total += n; }
        CHECK(worst <= 15);                                      // a pass never moves more than its budget
        CHECK(total > 0);
        CHECK_NEAR(v.SolidVolume(), before, before * 0.02);      // volume kept (to the voxel's rounding)
        // stone does not slump
        VoxelTerrain st = MakeField();
        for (auto& m : st.material) m = 4;
        st.Edit({0, 14, 0}, 4.0f, 30.0f, VoxelTerrain::EditOptions{});
        CHECK(st.Slump(100) == 0);
        // and the pass only looks near the edit: far-away loose voxels stay where they are
        VoxelTerrain far = MakeField();
        for (auto& m : far.material) m = 2;
        far.density[(std::size_t)far.Index(30, 24, 30)] = 3.0f;  // a floating sand voxel nowhere near an edit
        far.Set(5, 5, 5, far.density[(std::size_t)far.Index(5, 5, 5)]);
        far.Slump(1000);
        CHECK(far.density[(std::size_t)far.Index(30, 24, 30)] > far.iso);
    }

    // --- loose pieces: cut out, counted, meshed ---------------------------------------------------------------------------
    {
        VoxelTerrain v = MakeField(30, 30, 30);
        for (auto& m : v.material) m = 4;
        // a 3x3x3 block standing on a one-voxel post, then the post is cut
        for (int y = 20; y < 23; ++y) for (int x = 13; x < 16; ++x) for (int z = 13; z < 16; ++z) v.Set(x, y, z, 3.0f);
        for (int y = 12; y < 20; ++y) v.Set(14, y, 14, 3.0f);
        v.Set(14, 19, 14, -3.0f);                                // the post is cut: the block floats
        const double volBefore = v.SolidVolume();
        const auto pieces = v.ExtractFloaters(4);
        CHECK(pieces.size() == 1);
        if (!pieces.empty()) {
            CHECK(pieces[0].cells.size() == 27);
            CHECK(pieces[0].material == 4);
            CHECK(pieces[0].volume > 20.0 && pieces[0].volume < 28.0);
            const Mesh pm = v.PieceMesh(pieces[0]);
            std::cout << "  piece mesh: " << pm.vertices.size() << " vertices, " << pm.TriangleCount() << " triangles\n";
            CHECK(pm.TriangleCount() > 10 && pm.HasNormals());
            CHECK_NEAR(volBefore - v.SolidVolume(), pieces[0].volume, 1e-3);   // what left the field is what the piece carries
        }
        CHECK(v.ExtractFloaters(4).empty());                     // nothing more floats
        // the slab itself is never a piece
        VoxelTerrain w = MakeField(30, 30, 30);
        w.Set(15, 6, 15, w.density[(std::size_t)w.Index(15, 6, 15)]);
        CHECK(w.ExtractFloaters(4).empty());
    }

    // --- saving: materials and digger options round-trip; an older digger line still loads ------------------------------------
    {
        Scene s("SAVE"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Terrain");
        auto* v = go->AddComponent<VoxelTerrain>();
        v->Resize(20, 16, 20); v->voxelSize = 1.0f; v->Generate(0.5f, 3.0f, 0.0f, 4u); v->GenerateMaterials(4u);
        v->materials[4].hardness = 5.5f; v->materials[2].drop = "beach"; v->materials[0].loose = false;
        v->Apply();
        auto* d = go->AddComponent<VoxelDigger>();
        d->collect = VoxelDigger::Collect::Both; d->needsMaterial = true; d->power = 3.5f; d->hardnessScale = 1.5f;
        d->shape = VoxelTerrain::EditOptions::Shape::Cube; d->depthLimit = 4.0f; d->protectBelow = 1.0f; d->snapToGrid = true;
        d->debrisMax = 20; d->slump = true; d->collapse = true; d->hudTarget = "Counter"; d->addMaterial = 2;
        const std::string text = SceneSerializer::Serialize(s);
        Scene s2("LOAD"); s2.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* v2 = s2.Find("Terrain")->GetComponent<VoxelTerrain>();
        auto* d2 = s2.Find("Terrain")->GetComponent<VoxelDigger>();
        CHECK(v2->UseMaterials() && v2->materials.size() == 7 && v2->material == v->material);
        CHECK(v2->materials[4].hardness == 5.5f && v2->materials[2].drop == "beach" && !v2->materials[0].loose);
        CHECK(d2->collect == VoxelDigger::Collect::Both && d2->needsMaterial && d2->power == 3.5f && d2->hardnessScale == 1.5f);
        CHECK(d2->shape == VoxelTerrain::EditOptions::Shape::Cube && d2->depthLimit == 4.0f && d2->protectBelow == 1.0f && d2->snapToGrid);
        CHECK(d2->debrisMax == 20 && d2->slump && d2->collapse && d2->hudTarget == "Counter" && d2->addMaterial == 2);
        // a scene without materials has no material records, and the digger line of an older build (no options) loads as before
        Scene s3("OLD"); s3.physicsEnabled = false;
        GameObject* g3 = s3.CreateGameObject("Terrain");
        auto* v3 = g3->AddComponent<VoxelTerrain>(); v3->Resize(12, 12, 12); v3->FillSlab(0.5f); v3->Apply();
        g3->AddComponent<VoxelDigger>();
        std::string t3 = SceneSerializer::Serialize(s3);
        CHECK(t3.find("voxelmats") == std::string::npos);
        const std::size_t at = t3.find("  voxeldigger ");
        CHECK(at != std::string::npos);
        const std::size_t eol = t3.find('\n', at);
        std::string line = t3.substr(at, eol - at);
        // cut the line after the 12th value (the opposite button), as older builds wrote it
        std::size_t pos = 0; int fields = 0;
        for (std::size_t i = 0; i < line.size() && fields < 13; ++i) if (line[i] == ' ' && i > 0 && line[i - 1] != ' ') { ++fields; pos = i; }
        t3.replace(at, eol - at, line.substr(0, pos));
        Scene s4("OLD2"); s4.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(s4, t3));
        auto* d4 = s4.Find("Terrain")->GetComponent<VoxelDigger>();
        CHECK(d4 != nullptr && d4->collect == VoxelDigger::Collect::Vanish && !d4->needsMaterial && d4->power > 1.0e8f);
        CHECK(!s4.Find("Terrain")->GetComponent<VoxelTerrain>()->UseMaterials());
    }

    TEST_MAIN_RESULT();
}
