#include "test_framework.hpp"
#include "voxel_mesh_util.hpp"
#include <cmath>
#include <iostream>

using namespace okay;
using namespace voxeltest;

// Voxel edit correctness. Written BEFORE the edit-path rework (rule 24) and run against the old code first:
//   1. after a dig, Apply() must give exactly the mesh a full rebuild (BuildMesh) gives. The old dirty
//      range ignored that a vertex normal reads the density two cells away, so cubes next to the dug
//      area kept stale normals (a lighting seam at the edge of the hole): 4 of 60 random edits differed.
//   2. a brush with radius 0 or a NaN strength / position wrote NaN into the density field (5 of the 7
//      checks below failed on the old code).
int main() {
    RUN_SUITE("voxel_edit");

    // --- 1. Apply() after edits gives exactly what a full rebuild gives -----------------------------------
    {
        Scene s("EDIT"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Voxel");
        auto* v = go->AddComponent<VoxelTerrain>();
        v->Resize(40, 26, 40); v->voxelSize = 1.0f;
        v->Generate(0.5f, 4.0f, 0.0f, 5u);
        v->Apply();
        CHECK(go->GetComponent<MeshRenderer>() != nullptr);
        CHECK(Differences(GatherDrawn(go), v->BuildMesh()) == 0);   // the very first build agrees

        Lcg rng(1234u);
        int badSteps = 0;
        const int steps = 60;
        for (int i = 0; i < steps; ++i) {
            RandomEdit(*v, rng, i);
            v->Apply();
            if (Differences(GatherDrawn(go), v->BuildMesh()) != 0) ++badSteps;
        }
        std::cout << "  incremental != full rebuild after " << badSteps << " of " << steps << " edit steps\n";
        CHECK(badSteps == 0);   // old code: 4 of 60 (stale normals beside the dug area)
    }

    // --- 2. A brush that cannot make sense must leave the field untouched --------------------------------
    {
        VoxelTerrain v;
        v.Resize(16, 16, 16); v.voxelSize = 1.0f; v.FillSlab(0.6f);
        const Vec3 p{0.5f, 5.0f, 0.5f};     // exactly on a grid point (x = 8, y = 5, z = 8)
        v.Dig(p, 0.0f, 3.0f);                        // radius 0: a division by zero used to write NaN
        CHECK(AllFinite(v));
        v.Dig(p, -2.0f, 3.0f);                       // negative radius
        CHECK(AllFinite(v));
        v.Dig(p, 3.0f, std::nanf(""));               // NaN strength
        CHECK(AllFinite(v));
        v.Dig({std::nanf(""), 5.0f, 0.0f}, 3.0f, 3.0f);   // NaN position
        CHECK(AllFinite(v));
        v.SmoothAt(p, 0.0f, 1.0f);
        CHECK(AllFinite(v));
        v.SmoothAt(p, 3.0f, std::nanf(""));
        CHECK(AllFinite(v));
        v.DigBox({0, 0, 0}, {2, 2, std::nanf("")}, 3.0f);
        CHECK(AllFinite(v));
    }

    TEST_MAIN_RESULT();
}
