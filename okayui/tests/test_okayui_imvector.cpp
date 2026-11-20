// OkayUI::ImVector: push_back of one of its own elements while it has to grow (v.push_back(v[0]) on a full vector).
// Growing frees the old block, so the value must be copied out before that; before the fix this read freed memory
// (shows under AddressSanitizer as heap-use-after-free, and as garbage values without it).
#include "okay/UI/OkayUI.hpp"
#include <cstdio>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s (line %d)\n", #c, __LINE__); ++g_fail; } } while (0)

struct P { float x, y; };

int main() {
    // Exactly full each time: every push_back below has to grow.
    for (int n = 1; n <= 64; ++n) {
        OkayUI::ImVector<P> v;
        for (int i = 0; i < n; ++i) v.push_back(P{float(i) + 0.5f, float(i) * 2.0f});
        while (v.Size < v.Capacity) v.push_back(P{-1.0f, -1.0f});   // fill to capacity so the next push grows
        const P first = v[0];
        const int before = v.Size;
        v.push_back(v[0]);
        CHECK(v.Size == before + 1);
        CHECK(v[v.Size - 1].x == first.x && v[v.Size - 1].y == first.y);
        v.push_back(v[v.Size - 1]);   // the last element too
        CHECK(v[v.Size - 1].x == first.x && v[v.Size - 1].y == first.y);
    }
    // The ordinary case still works.
    OkayUI::ImVector<int> w;
    for (int i = 0; i < 1000; ++i) w.push_back(i);
    bool ok = w.Size == 1000;
    for (int i = 0; i < 1000 && ok; ++i) ok = w[i] == i;
    CHECK(ok);
    std::printf(g_fail ? "okayui_imvector: %d failure(s)\n" : "okayui_imvector: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
