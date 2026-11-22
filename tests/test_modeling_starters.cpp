// Every one-click starter model builds headlessly into a valid, closed mesh with a sane size and a colour per face.
#include "ModelingStarters.hpp"
#include "ModelingEasy.hpp"
#include <cmath>
#include <cstdio>
using namespace oked::easymodel;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main() {
    CHECK(Starters().size() == 7);
    for (const Starter& s : Starters()) {
        okay::Mesh m = BuildStarter(s.id);
        const int tris = m.TriangleCount(), verts = (int)m.vertices.size();
        std::printf("%-8s verts %4d tris %4d\n", s.id, verts, tris);
        CHECK(tris >= 24 && tris <= 3000);
        CHECK(verts >= 12 && verts <= 3 * tris);
        CHECK(m.triangles.size() % 3 == 0);
        for (int t : m.triangles) CHECK(t >= 0 && t < verts);
        CHECK((int)m.triColors.size() == tris);                       // every face has a colour
        for (const okay::Vec3& v : m.vertices) CHECK(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z));
        const auto top = m.Topology();
        CHECK(top.Closed());                                          // no holes, no half-shared edges
        CHECK(top.degenerateTris <= 100);                              // the ball's poles leave a few zero-area slivers
        const okay::Vec3 sz = m.Size();
        CHECK(sz.x > 0.2f && sz.y > 0.2f && sz.z > 0.02f && sz.x < 10 && sz.y < 10 && sz.z < 10);
        CHECK(FindBannedWord(s.name).empty() && FindBannedWord(s.tip).empty());
        CHECK(m.name == s.id);
    }
    CHECK(BuildStarter("nope").TriangleCount() == 0);
    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("modeling_starters: all passed\n");
    return 0;
}
