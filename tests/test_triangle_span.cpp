// TriangleRows / RasterDepthTriangle (okay/Render/TriangleSpan.hpp): the row span shortcut of the shadow depth pass must
// never skip a pixel the float edge tests accept, so the depth buffer is bit for bit what the plain bounding box loop gives.
//  - span check: for many random triangles (large, thin, slivers, near horizontal or vertical edges, vertices on pixel
//    centres and on pixel corners, far outside the map, with the 0 and the -1e-4 edge threshold) every pixel the plain float
//    tests accept lies inside the span of its row;
//  - depth check: RasterDepthTriangle against a copy of the plain loop, one fresh buffer per triangle, memcmp equal;
//  - the span really is narrower for a big diagonal triangle (the shortcut does something).
#include "test_framework.hpp"
#include "okay/Render/TriangleSpan.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace okay::detail;

namespace {

struct Lcg {
    std::uint64_t s;
    explicit Lcg(std::uint64_t seed) : s(seed) {}
    std::uint32_t Next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return (std::uint32_t)(s >> 33); }
    float Unit() { return (float)(Next() & 0xFFFFFF) / 16777216.0f; }
    float Range(float a, float b) { return a + (b - a) * Unit(); }
    int Below(int n) { return (int)(Next() % (std::uint32_t)n); }
};

// The loop the cascades ran before the shortcut (copied here as the reference).
void RefRaster(float* depth, int S, const float sxv[3], const float syv[3], const float szv[3]) {
    int minx = (int)std::floor(std::fmin(sxv[0], std::fmin(sxv[1], sxv[2])));
    int maxx = (int)std::ceil (std::fmax(sxv[0], std::fmax(sxv[1], sxv[2])));
    int miny = (int)std::floor(std::fmin(syv[0], std::fmin(syv[1], syv[2])));
    int maxy = (int)std::ceil (std::fmax(syv[0], std::fmax(syv[1], syv[2])));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= S) maxx = S - 1;
    if (maxy >= S) maxy = S - 1;
    float area = (sxv[1] - sxv[0]) * (syv[2] - syv[0]) - (sxv[2] - sxv[0]) * (syv[1] - syv[0]);
    if (area == 0.0f) return;
    float inv = 1.0f / area;
    for (int y = miny; y <= maxy; ++y)
        for (int x = minx; x <= maxx; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((sxv[1] - px) * (syv[2] - py) - (sxv[2] - px) * (syv[1] - py)) * inv;
            float w1 = ((sxv[2] - px) * (syv[0] - py) - (sxv[0] - px) * (syv[2] - py)) * inv;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * szv[0] + w1 * szv[1] + w2 * szv[2];
            float& dref = depth[(std::size_t)y * S + x];
            if (z < dref) dref = z;
        }
}

// One coordinate of a random triangle: the awkward kinds first.
float Coord(Lcg& r, int S, int kind) {
    switch (kind) {
        case 0: return r.Range(-0.25f * S, 1.25f * S);                       // around the map
        case 1: return (float)r.Below(S + 1);                                // on pixel corners
        case 2: return (float)r.Below(S) + 0.5f;                             // on pixel centres
        case 3: return r.Range(-2.0e6f, 2.0e6f);                             // far away
        case 4: return r.Range(0.0f, (float)S);                              // inside
        default: return (float)r.Below(S + 1) + (r.Below(2) ? 0.5f : 0.0f) + r.Range(-1e-3f, 1e-3f);   // near the grid
    }
}

} // namespace

int main() {
    Lcg r(31337);
    const int sizes[] = {48, 130, 256};

    RUN_SUITE("triangle spans: every pixel the float tests accept is inside the span");
    {
        long tris = 0, narrowed = 0, pixelsPassing = 0, rowsChecked = 0, columnsSkipped = 0, columnsInBoxes = 0;
        for (int iter = 0; iter < 6000; ++iter) {
            const int S = sizes[r.Below(3)];
            const int kind = r.Below(6);
            float sx[3], sy[3];
            for (int k = 0; k < 3; ++k) { sx[k] = Coord(r, S, kind); sy[k] = Coord(r, S, r.Below(6) < 4 ? kind : r.Below(6)); }
            if (r.Below(7) == 0) sy[1] = sy[2];                              // a horizontal edge
            if (r.Below(7) == 0) sx[0] = sx[1];                              // a vertical edge
            if (r.Below(9) == 0) { sx[2] = 0.5f * (sx[0] + sx[1]) + r.Range(-1e-3f, 1e-3f); sy[2] = 0.5f * (sy[0] + sy[1]); }   // a sliver
            const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
            if (area == 0.0f) continue;
            const float inv = 1.0f / area;
            const float edgeMin = r.Below(2) ? 0.0f : -1e-4f;
            int minx = (int)std::floor(std::fmin(sx[0], std::fmin(sx[1], sx[2])));
            int maxx = (int)std::ceil (std::fmax(sx[0], std::fmax(sx[1], sx[2])));
            int miny = (int)std::floor(std::fmin(sy[0], std::fmin(sy[1], sy[2])));
            int maxy = (int)std::ceil (std::fmax(sy[0], std::fmax(sy[1], sy[2])));
            if (minx < 0) minx = 0;
            if (miny < 0) miny = 0;
            if (maxx >= S) maxx = S - 1;
            if (maxy >= S) maxy = S - 1;
            if (minx > maxx || miny > maxy) continue;
            ++tris;
            TriangleRows rows;
            if (!rows.Init(sx, sy, inv, edgeMin, minx, maxx)) continue;      // not narrowed: nothing to check
            ++narrowed;
            for (int y = miny; y <= maxy; ++y) {
                int xa = minx, xb = maxx;
                rows.Row(y, xa, xb);
                ++rowsChecked;
                columnsSkipped += (maxx - minx + 1) - (xb >= xa ? xb - xa + 1 : 0);
                columnsInBoxes += maxx - minx + 1;
                for (int x = minx; x <= maxx; ++x) {
                    const float px = x + 0.5f, py = y + 0.5f;
                    const float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) * inv;
                    const float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) * inv;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < edgeMin || w1 < edgeMin || w2 < edgeMin) continue;
                    ++pixelsPassing;
                    const bool inside = x >= xa && x <= xb;
                    CHECK(inside);
                    if (!inside) {
                        std::printf("  MISS: iter %d kind %d S %d y %d x %d span [%d, %d] edgeMin %g tri (%g,%g) (%g,%g) (%g,%g)\n",
                                    iter, kind, S, y, x, xa, xb, edgeMin, sx[0], sy[0], sx[1], sy[1], sx[2], sy[2]);
                        goto next;
                    }
                }
            }
            next:;
        }
        std::printf("  %ld triangles, %ld narrowed, %ld rows, %ld passing pixels, %.1f%% of the columns skipped\n",
                    tris, narrowed, rowsChecked, pixelsPassing,
                    columnsInBoxes ? 100.0 * (double)columnsSkipped / (double)columnsInBoxes : 0.0);
        CHECK(narrowed > 3000);
        CHECK(pixelsPassing > 100000);
    }

    RUN_SUITE("triangle spans: RasterDepthTriangle gives the buffer of the plain loop");
    {
        int same = 0, total = 0;
        for (int iter = 0; iter < 4000; ++iter) {
            const int S = sizes[r.Below(3)];
            const int kind = r.Below(6);
            float sx[3], sy[3], sz[3];
            for (int k = 0; k < 3; ++k) { sx[k] = Coord(r, S, kind); sy[k] = Coord(r, S, kind); sz[k] = r.Range(-1.0f, 1.0f); }
            if (r.Below(7) == 0) sy[1] = sy[2];
            if (r.Below(11) == 0) sx[2] = sx[1];                             // zero area
            std::vector<float> a((std::size_t)S * S, 2.0f), b((std::size_t)S * S, 2.0f);
            RefRaster(a.data(), S, sx, sy, sz);
            RasterDepthTriangle(b.data(), S, sx, sy, sz);
            ++total;
            if (std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0) ++same;
            else std::printf("  DIFFERENT: iter %d kind %d S %d\n", iter, kind, S);
        }
        CHECK(same == total);
        // Several triangles into one buffer (what a mesh does): same again.
        for (int rep = 0; rep < 30; ++rep) {
            const int S = 200;
            std::vector<float> a((std::size_t)S * S, 2.0f), b((std::size_t)S * S, 2.0f);
            for (int t = 0; t < 40; ++t) {
                float sx[3], sy[3], sz[3];
                for (int k = 0; k < 3; ++k) { sx[k] = r.Range(-20, 220); sy[k] = r.Range(-20, 220); sz[k] = r.Range(-1, 1); }
                RefRaster(a.data(), S, sx, sy, sz);
                RasterDepthTriangle(b.data(), S, sx, sy, sz);
            }
            CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
        }
    }

    RUN_SUITE("triangle spans: a big diagonal triangle skips about half of its box");
    {
        const float sx[3] = {0.0f, 1000.0f, 0.0f}, sy[3] = {0.0f, 0.0f, 1000.0f};
        const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        TriangleRows rows;
        CHECK(rows.Init(sx, sy, 1.0f / area, 0.0f, 0, 999));
        long kept = 0;
        for (int y = 0; y < 1000; ++y) { int xa = 0, xb = 999; rows.Row(y, xa, xb); kept += xb >= xa ? xb - xa + 1 : 0; }
        std::printf("  kept %ld of 1000000 columns\n", kept);
        CHECK(kept < 520000);   // half is 500000; a few columns of slack per row
        CHECK(kept >= 500000);
    }

    TEST_MAIN_RESULT();
}
