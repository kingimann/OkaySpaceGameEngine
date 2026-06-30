#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Voxel digging quality: edits report whether they actually changed the field (so the
// digger can skip needless full rebuilds), and RemoveFloaters deletes the small
// disconnected "crumbs" a dig leaves behind that the player can't reach.
int main() {
    RUN_SUITE("voxel_dig");

    VoxelTerrain v;
    v.Resize(16, 16, 16);
    v.iso = 0.0f;
    v.FillSlab(0.5f);                 // bottom half solid — something to carve

    // Digging into solid changes the field; digging high up in empty air does not.
    Vec3 inSolid{0.0f, 1.0f, 0.0f};
    Vec3 inAir{0.0f, (v.ny - 1) * v.voxelSize, 0.0f};
    CHECK(v.Dig(inSolid, 2.0f, 5.0f));     // carved -> changed
    CHECK(!v.Dig(inAir, 2.0f, 5.0f));      // nothing there -> no change

    // Floater cleanup: plant a tiny isolated solid blob high in the air, well above
    // the slab, then sweep it away while the big slab survives.
    auto solidCount = [&]() {
        int n = 0; for (int i = 0; i < v.Count(); ++i) if (v.density[i] > v.iso) ++n; return n;
    };
    v.Set(8, 13, 8, 5.0f);                 // a single floating solid voxel (a crumb)
    int before = solidCount();
    CHECK(before > 1);
    int cleared = v.RemoveFloaters(8);     // remove components smaller than 8 voxels
    CHECK(cleared >= 1);                   // the crumb was removed
    CHECK(!v.SolidAt({0.0f, 13.0f * v.voxelSize, 0.0f}));   // gone from the air
    // The big slab (one large connected component) is untouched — check a corner far
    // from where we dug.
    CHECK(v.SolidAt({4.5f, 0.4f, 4.5f}));

    // A field with no crumbs is left alone.
    CHECK(v.RemoveFloaters(8) == 0);

    // Denser default grid (less "low poly").
    VoxelTerrain d;
    CHECK(d.nx >= 64 && d.nz >= 64);

    // DigBox carves a sharp rectangular void inside the slab; voxels outside it stay.
    {
        VoxelTerrain b;
        b.Resize(16, 16, 16); b.iso = 0.0f; b.FillSlab(0.6f);
        CHECK(b.SolidAt({0.0f, 1.0f, 0.0f}));                 // solid before
        bool ch = b.DigBox({-2, 0.5f, -2}, {2, 3.0f, 2}, 10.0f);
        CHECK(ch);
        CHECK(!b.SolidAt({0.0f, 1.0f, 0.0f}));                // carved out
        CHECK(b.SolidAt({6.0f, 0.3f, 6.0f}));                 // outside the box: untouched
        CHECK(!b.DigBox({-2, 0.5f, -2}, {2, 3.0f, 2}, 10.0f));// already air -> no change
    }

    TEST_MAIN_RESULT();
}
