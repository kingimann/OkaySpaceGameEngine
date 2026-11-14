// World streaming foundation: cell addressing (docs/foundations/world_streaming.md).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/WorldGrid.hpp"
#include <cmath>
#include <limits>
#include <set>

using namespace okay;
using namespace okay::worldpart;

static const float kInf = std::numeric_limits<float>::infinity();
static const float kNaN = std::numeric_limits<float>::quiet_NaN();

int main() {
    RUN_SUITE("world_grid");

    // ---- a bad cell size makes an invalid grid; nothing is addressable ----
    for (float bad : {0.0f, -1.0f, kNaN, kInf, -kInf}) {
        CHECK(!WorldGrid::IsValidCellSize(bad));
        WorldGrid g(bad);
        CHECK(!g.Valid());
        CellCoord c{7, 7, 7};
        CHECK(!g.CellOf(Vec3{1, 2, 3}, c));
        CHECK(c.x == 7);   // untouched
        Vec3 lo, hi;
        CHECK(!g.CellBox({0, 0, 0}, lo, hi));
        CHECK(std::isinf(g.DistanceToCell(Vec3{0, 0, 0}, {0, 0, 0})));
        CHECK(g.Neighbours({0, 0, 0}).empty());
        std::vector<CellCoord> v;
        CHECK(!g.CellsInRadius(Vec3{0, 0, 0}, 10.0f, v));
    }
    CHECK(WorldGrid::IsValidCellSize(0.001f));
    CHECK(WorldGrid(64.0f).Valid());

    // ---- floor addressing, negative coordinates ----
    {
        WorldGrid g(64.0f, GridPlane::XZ);
        CellCoord c;
        CHECK(g.CellOf(Vec3{0, 0, 0}, c) && c == CellCoord({0, 0, 0}));
        CHECK(g.CellOf(Vec3{63.999f, 0, 0}, c) && c.x == 0);
        CHECK(g.CellOf(Vec3{64.0f, 0, 0}, c) && c.x == 1);          // the low edge belongs to the cell
        CHECK(g.CellOf(Vec3{-0.001f, 0, 0}, c) && c.x == -1);       // not truncated to 0
        CHECK(g.CellOf(Vec3{-64.0f, 0, 0}, c) && c.x == -1);
        CHECK(g.CellOf(Vec3{-64.01f, 0, 0}, c) && c.x == -2);
        CHECK(g.CellOf(Vec3{0, 0, -130.0f}, c) && c.z == -3);
        CHECK(g.CellOf(Vec3{10, 1e30f, 10}, c) && c.y == 0);          // XZ ignores height, even a silly one
        CHECK(g.CellOf(Vec3{-0.0f, 0, -0.0f}, c) && c.x == 0 && c.z == 0);
    }
    {
        WorldGrid g(10.0f, GridPlane::XY);
        CellCoord c;
        CHECK(g.CellOf(Vec3{25, -15, 1e30f}, c) && c == CellCoord({2, -2, 0}));
        WorldGrid g3(10.0f, GridPlane::XYZ);
        CHECK(g3.CellOf(Vec3{25, -15, -5}, c) && c == CellCoord({2, -2, -1}));
        CHECK(!g3.CellOf(Vec3{25, -15, 1e30f}, c));
    }

    // ---- huge and non-finite positions ----
    {
        WorldGrid g(1.0f, GridPlane::XYZ);
        CellCoord c;
        CHECK(!g.CellOf(Vec3{kNaN, 0, 0}, c));
        CHECK(!g.CellOf(Vec3{0, kInf, 0}, c));
        CHECK(!g.CellOf(Vec3{0, 0, -kInf}, c));
        CHECK(!g.CellOf(Vec3{3.0e38f, 0, 0}, c));
        CHECK(!g.CellOf(Vec3{-3.0e38f, 0, 0}, c));
        CHECK(g.CellOf(Vec3{static_cast<float>(kMaxCellIndex), 0, 0}, c) && c.x == kMaxCellIndex);
        CHECK(!g.CellOf(Vec3{static_cast<float>(kMaxCellIndex) + 2.0f, 0, 0}, c));
        CHECK(!g.CellOf(Vec3{-static_cast<float>(kMaxCellIndex) - 2.0f, 0, 0}, c));
        WorldGrid tiny(1e-30f);
        CHECK(!tiny.CellOf(Vec3{1.0f, 0, 0}, c));    // would overflow the index
        CHECK(tiny.CellOf(Vec3{0, 0, 0}, c));
        WorldGrid big(1e30f);
        CHECK(big.CellOf(Vec3{3.5e30f, 0, -2.5e30f}, c) && c.x == 3 && c.z == -3);
    }

    // ---- boxes, centres, distances ----
    {
        WorldGrid g(10.0f, GridPlane::XZ);
        Vec3 lo, hi;
        CHECK(g.CellBox({-1, 0, 2}, lo, hi));
        CHECK_NEAR(lo.x, -10.0f, 1e-4); CHECK_NEAR(hi.x, 0.0f, 1e-4);
        CHECK_NEAR(lo.z, 20.0f, 1e-4);  CHECK_NEAR(hi.z, 30.0f, 1e-4);
        CHECK(std::isinf(lo.y) && lo.y < 0 && std::isinf(hi.y) && hi.y > 0);
        Vec3 mid;
        CHECK(g.CellCenter({-1, 0, 2}, mid));
        CHECK_NEAR(mid.x, -5.0f, 1e-4); CHECK_NEAR(mid.z, 25.0f, 1e-4); CHECK_NEAR(mid.y, 0.0f, 1e-6);
        CHECK(!g.CellBox({kMaxCellIndex + 1, 0, 0}, lo, hi));
        CHECK(!g.CellBox({0, 3, 0}, lo, hi));     // XZ: y must be 0
        CHECK_NEAR(g.DistanceToCell(Vec3{-5, 100, 25}, {-1, 0, 2}), 0.0f, 1e-5);     // inside (height ignored)
        CHECK_NEAR(g.DistanceToCell(Vec3{5, 0, 25}, {-1, 0, 2}), 5.0f, 1e-4);        // beside
        CHECK_NEAR(g.DistanceToCell(Vec3{3, 0, 34}, {-1, 0, 2}), 5.0f, 1e-4);        // diagonal: 3,4 -> 5
        CHECK(std::isinf(g.DistanceToCell(Vec3{kNaN, 0, 0}, {0, 0, 0})));
        // CellOf and CellBox agree for many points.
        for (int i = 0; i < 2000; ++i) {
            const float x = (static_cast<float>((i * 7919) % 20001) - 10000.0f) * 0.37f;
            const float z = (static_cast<float>((i * 104729) % 20001) - 10000.0f) * 0.41f;
            CellCoord c;
            CHECK(g.CellOf(Vec3{x, 0, z}, c));
            Vec3 l2, h2;
            CHECK(g.CellBox(c, l2, h2));
            CHECK(x >= l2.x - 1e-3f && x <= h2.x + 1e-3f && z >= l2.z - 1e-3f && z <= h2.z + 1e-3f);
        }
    }

    // ---- neighbours ----
    {
        CHECK(WorldGrid(1.0f, GridPlane::XZ).Neighbours({0, 0, 0}).size() == 8);
        CHECK(WorldGrid(1.0f, GridPlane::XY).Neighbours({0, 0, 0}).size() == 8);
        CHECK(WorldGrid(1.0f, GridPlane::XYZ).Neighbours({0, 0, 0}).size() == 26);
        CHECK(WorldGrid(1.0f, GridPlane::XZ).Neighbours({0, 0, 0}, 2).size() == 24);
        CHECK(WorldGrid(1.0f, GridPlane::XZ).Neighbours({0, 0, 0}, 1, true).size() == 9);
        CHECK(WorldGrid(1.0f, GridPlane::XZ).Neighbours({0, 0, 0}, 0).empty());
        CHECK(WorldGrid(1.0f, GridPlane::XZ).Neighbours({0, 0, 0}, -5).empty());
        CHECK(WorldGrid(1.0f, GridPlane::XZ).Neighbours({0, 0, 0}, 1000, true).size() == 65u * 65u);   // clamped to ring 32
        // At the edge of the addressable world the missing side is simply not listed.
        CHECK(WorldGrid(1.0f, GridPlane::XZ).Neighbours({kMaxCellIndex, 0, 0}).size() == 5);
        CHECK(WorldGrid(1.0f, GridPlane::XZ).Neighbours({kMaxCellIndex + 5, 0, 0}).empty());
        // Negative side, same count.
        CHECK(WorldGrid(1.0f, GridPlane::XZ).Neighbours({-kMaxCellIndex, 0, -kMaxCellIndex}).size() == 3);
    }

    // ---- cells in a radius ----
    {
        WorldGrid g(10.0f, GridPlane::XZ);
        std::vector<CellCoord> v;
        CHECK(g.CellsInRadius(Vec3{5, 0, 5}, 0.0f, v) && v.size() == 1 && v[0] == CellCoord({0, 0, 0}));
        CHECK(g.CellsInRadius(Vec3{5, 0, 5}, 5.0f, v));
        CHECK(v.size() == 5);                        // own cell + the 4 sides (corners are 7.07 away)
        CHECK(v[0] == CellCoord({0, 0, 0}));         // nearest first
        CHECK(g.CellsInRadius(Vec3{5, 0, 5}, 8.0f, v) && v.size() == 9);
        CHECK(g.CellsInRadius(Vec3{-5, 0, -5}, 8.0f, v) && v.size() == 9 && v[0] == CellCoord({-1, 0, -1}));
        for (std::size_t i = 1; i < v.size(); ++i)
            CHECK(g.DistanceToCell(Vec3{-5, 0, -5}, v[i - 1]) <= g.DistanceToCell(Vec3{-5, 0, -5}, v[i]));
        CHECK(!g.CellsInRadius(Vec3{5, 0, 5}, kNaN, v) && v.empty());
        CHECK(!g.CellsInRadius(Vec3{5, 0, 5}, -1.0f, v));
        CHECK(!g.CellsInRadius(Vec3{5, 0, 5}, kInf, v));
        CHECK(!g.CellsInRadius(Vec3{5, 0, 5}, 1e30f, v));
        CHECK(!g.CellsInRadius(Vec3{kNaN, 0, 5}, 10.0f, v));
        CHECK(!g.CellsInRadius(Vec3{5, 0, 5}, 1000.0f, v, 50));     // more cells than the cap
        CHECK(g.CellsInRadius(Vec3{5, 0, 5}, 1000.0f, v, 100000) && v.size() > 1000);
    }

    // ---- names and packing ----
    {
        const CellCoord samples[] = {{0, 0, 0}, {-3, 0, 2}, {kMaxCellIndex, -kMaxCellIndex, 0}, {12, 34, -56}, {-1, -1, -1}};
        for (const CellCoord& c : samples) {
            CellCoord back;
            CHECK(WorldGrid::ParseCellName(WorldGrid::CellName(c), back) && back == c);
        }
        CHECK(WorldGrid::CellName({-3, 0, 2}) == "cell_n3_0_2");
        CellCoord c;
        for (const char* bad : {"", "cell", "cell_1_2", "cell_1_2_3_4", "cell_1_2_3x", "cell__1_2", "cell_n_1_2", "cell_-1_2_3",
                                "cell_1048576_0_0", "cell_99999999999999999999_0_0", "Cell_1_2_3", " cell_1_2_3", "cell_1_2_3 "})
            CHECK(!WorldGrid::ParseCellName(bad, c));
        std::set<std::uint64_t> packs;
        int n = 0;
        for (int x = -6; x <= 6; ++x) for (int y = -6; y <= 6; ++y) for (int z = -6; z <= 6; ++z) { packs.insert(PackCell({x, y, z})); ++n; }
        packs.insert(PackCell({kMaxCellIndex, kMaxCellIndex, kMaxCellIndex}));
        packs.insert(PackCell({-kMaxCellIndex, -kMaxCellIndex, -kMaxCellIndex}));
        CHECK(static_cast<int>(packs.size()) == n + 2);
    }

    // ---- normalize / range ----
    {
        WorldGrid xz(1.0f, GridPlane::XZ), xy(1.0f, GridPlane::XY);
        CHECK(xz.Normalize({1, 5, 2}) == CellCoord({1, 0, 2}));
        CHECK(xy.Normalize({1, 5, 2}) == CellCoord({1, 5, 0}));
        CHECK(!xz.InRange({1, 5, 2}));
        CHECK(xz.InRange({1, 0, 2}));
        CHECK(!xz.InRange({kMaxCellIndex + 1, 0, 0}));
        CHECK(xz.InRange({-kMaxCellIndex, 0, kMaxCellIndex}));
    }

    TEST_MAIN_RESULT();
}
