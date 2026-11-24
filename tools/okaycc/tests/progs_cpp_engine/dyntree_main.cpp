// Drives the engine's own engine/src/Physics/Core/DynamicTree.cpp, compiled unchanged by okay-cc
// (a class used from lambdas in its member functions, member function templates taking F&&,
// static constexpr members, braced temporaries and ?: of objects).
#include "okay/Physics/Core/DynamicTree.hpp"

extern "C" int printf(const char *fmt, ...);

using okay::phys::Aabb;
using okay::phys::V3;

static Aabb box(float x, float y, float z, float r) { return Aabb{V3{x - r, y - r, z - r}, V3{x + r, y + r, z + r}}; }

int main() {
    okay::phys::DynamicTree t;
    int ids[40];
    for (int i = 0; i < 40; ++i) {
        float x = (float)((i * 7) % 13), y = (float)((i * 5) % 11), z = (float)(i % 3);
        ids[i] = t.Create(box(x, y, z, 0.5f), (std::uint64_t)(1000 + i));
    }
    printf("leaves %d nodes %d height %d valid %d\n", t.LeafCount(), t.NodeCount(), t.Height(), t.Validate());
    printf("ratio %.4f root %d\n", t.AreaRatio(), t.Root());
    for (int i = 0; i < 40; i += 3) t.Move(ids[i], box((float)i * 0.25f, 1, 2, 0.5f), V3{0.25f, 0, 0});
    for (int i = 1; i < 40; i += 5) t.Destroy(ids[i]);
    printf("after leaves %d nodes %d height %d valid %d\n", t.LeafCount(), t.NodeCount(), t.Height(), t.Validate());
    int hits = 0;
    unsigned long sum = 0;
    t.Query(box(4, 4, 1, 3), [&](int id) {
        ++hits;
        sum += t.User(id);
        return true;
    });
    printf("query %d %lu\n", hits, sum);
    int rays = 0;
    t.RayCast(V3{-5, 1, 2}, V3{1, 0, 0}, 100.0f, [&](int id, float maxT) {
        ++rays;
        const Aabb &b = t.FatAabb(id);
        float tt = b.lo.x + 5.0f;
        return tt < maxT ? tt : maxT;
    });
    printf("rays %d\n", rays);
    const Aabb &f = t.FatAabb(ids[0]);
    printf("fat %.3f %.3f %.3f %.3f\n", f.lo.x, f.lo.y, f.hi.x, f.hi.z);
    return 0;
}
