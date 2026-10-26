// Mesh::PropRand (the per-variant random used by the procedural nature props)
// multiplied signed ints, which overflows for any variant >= 6 — undefined
// behaviour that UBSan reports. It must keep producing exactly the same values
// (variants are saved per object and regenerate the same shape on load).
#include "test_framework.hpp"
#include <Okay.hpp>
#include <string>

using namespace okay;

// The intended hash, written with defined (unsigned, wrapping) arithmetic.
static float Ref(int variant, int key, float lo, float hi) {
    if (variant == 0) return (lo + hi) * 0.5f;
    unsigned x = (unsigned)variant * 374761393u + (unsigned)key * 668265263u;
    x = (x ^ (x >> 13)) * 1274126177u;
    float t = (float)((x ^ (x >> 16)) & 0xffffu) / 65535.0f;
    return lo + (hi - lo) * t;
}

int main() {
    RUN_SUITE("mesh_variant_hash");

    for (int v : {0, 1, 5, 6, 7, 99, 12345, -3, 2147483647})
        for (int k : {0, 1, 2, 17, 1000, -8})
            CHECK(Mesh::PropRand(v, k, -1.0f, 2.0f) == Ref(v, k, -1.0f, 2.0f));

    // Every variant-capable prop regenerates identically for a large variant.
    for (const char* n : {"Tree", "Pine", "Rock", "Bush", "PalmTree", "DeadTree", "Mushroom", "Cactus", "Crystal"}) {
        Mesh a = Mesh::FromNameSeeded(n, 4242);
        Mesh b = Mesh::FromNameSeeded(n, 4242);
        CHECK(!a.vertices.empty());
        CHECK(a.vertices.size() == b.vertices.size());
        bool same = a.vertices.size() == b.vertices.size();
        for (std::size_t i = 0; same && i < a.vertices.size(); ++i)
            same = a.vertices[i].x == b.vertices[i].x && a.vertices[i].y == b.vertices[i].y && a.vertices[i].z == b.vertices[i].z;
        CHECK(same);
    }

    TEST_MAIN_RESULT();
}
