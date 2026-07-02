#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <set>

using namespace okay;

int main() {
    RUN_SUITE("terrain_ops");

    // Terrace quantizes the heightmap into N flat bands.
    {
        Terrain t;
        t.Resize(24);
        t.Randomize(6.0f, 42u);
        t.Terrace(4);
        std::set<long> levels;
        for (float h : t.heights) levels.insert((long)std::lround(h * 1000.0f));   // bucket exact values
        CHECK(!t.heights.empty());
        CHECK((int)levels.size() <= 4);          // at most 4 distinct elevations
        CHECK((int)levels.size() >= 2);          // and it actually banded (not flat)
    }

    // SlopeAt: flat ground reads ~0 degrees; a ramp reads steeper.
    {
        Terrain flat;
        flat.Resize(8);
        flat.Flatten(0.0f);
        CHECK(flat.SlopeAt(0.0f, 0.0f) < 1.0f);

        Terrain ramp;
        ramp.Resize(8);
        // Build a constant X-slope: height rises with the X cell index.
        for (int z = 0; z < ramp.Dim(); ++z)
            for (int x = 0; x < ramp.Dim(); ++x)
                ramp.SetHeight(x, z, (float)x * ramp.CellSize());   // 45-degree ramp
        float s = ramp.SlopeAt(0.0f, 0.0f);
        CHECK(s > 30.0f && s < 60.0f);           // ~45 degrees
    }

    // Normalize remaps the height range so lowest -> lowY, highest -> highY.
    {
        Terrain t;
        t.Resize(16);
        t.Randomize(7.0f, 99u);
        t.Normalize(2.0f, 10.0f);
        float lo, hi; t.HeightRange(lo, hi);
        CHECK(std::fabs(lo - 2.0f) < 1e-3f);
        CHECK(std::fabs(hi - 10.0f) < 1e-3f);

        // A flat map collapses to lowY.
        Terrain flat;
        flat.Resize(8);
        flat.Flatten(5.0f);
        flat.Normalize(1.0f, 9.0f);
        float flo, fhi; flat.HeightRange(flo, fhi);
        CHECK(std::fabs(flo - 1.0f) < 1e-3f && std::fabs(fhi - 1.0f) < 1e-3f);
    }

    // Invert flips peaks and valleys while preserving the overall range.
    {
        Terrain t;
        t.Resize(12);
        t.Randomize(5.0f, 7u);
        float lo0, hi0; t.HeightRange(lo0, hi0);
        float mid = (lo0 + hi0) * 0.5f;
        float sample = t.GetHeight(3, 4);
        t.Invert();
        float lo1, hi1; t.HeightRange(lo1, hi1);
        CHECK(std::fabs((hi1 - lo1) - (hi0 - lo0)) < 1e-3f);   // range preserved
        CHECK(std::fabs(t.GetHeight(3, 4) - (2.0f * mid - sample)) < 1e-3f);
    }

    TEST_MAIN_RESULT();
}
