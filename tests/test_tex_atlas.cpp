// okay::tex atlas packer (okay/Texture/Atlas.hpp). Layout checks are invariants (no overlap, inside the sheet,
// padding kept); the hand-worked cases are exact sizes and the exact manifest text.
#include "test_framework.hpp"
#include "okay/Texture/Atlas.hpp"
#include <cstdlib>

using namespace okay::tex;

static bool Overlap(const AtlasPlacement& a, const AtlasPlacement& b, int gap) {
    return a.x < b.x + b.w + gap && b.x < a.x + a.w + gap && a.y < b.y + b.h + gap && b.y < a.y + a.h + gap;
}
static bool Valid(const AtlasResult& r, int gap) {
    for (std::size_t i = 0; i < r.placements.size(); ++i) {
        const auto& a = r.placements[i];
        if (a.x < 0 || a.y < 0 || a.x + a.w > r.width || a.y + a.h > r.height) return false;
        for (std::size_t j = i + 1; j < r.placements.size(); ++j)
            if (Overlap(a, r.placements[j], gap)) return false;
    }
    return true;
}
static bool IsPow2(int v) { return v > 0 && (v & (v - 1)) == 0; }

static void TestExact() {
    RUN_SUITE("tex atlas exact");
    // four 16x16 sprites = 1024 pixels = exactly a 32x32 sheet, fully used
    std::vector<AtlasItem> four = {{"a",16,16},{"b",16,16},{"c",16,16},{"d",16,16}};
    AtlasResult r = PackAtlas(four);
    CHECK(r.ok && r.width == 32 && r.height == 32);
    CHECK_NEAR(r.occupancy, 1.0, 1e-9);
    CHECK(Valid(r, 0));
    CHECK(r.placements.size() == 4 && r.placements[0].item == 0 && r.placements[3].item == 3);

    // one 1x1
    AtlasResult one = PackAtlas({{"dot",1,1}});
    CHECK(one.ok && one.width == 1 && one.height == 1);

    // empty input is fine and empty
    AtlasResult none = PackAtlas({});
    CHECK(none.ok && none.width == 0 && none.placements.empty());

    // non power of two sheet: 30x10 and 20x10 side by side -> 50x10 (non pot) trimmed
    AtlasOptions np; np.powerOfTwo = false;
    AtlasResult n = PackAtlas({{"p",30,10},{"q",20,10}}, np);
    CHECK(n.ok && n.height == 10 && n.width == 50);
    CHECK(Valid(n, 0));

    // padding 2: two 16x16 need 16 + 2 + 16 = 34 -> 64 wide pot, or 32x? Each gap respected.
    AtlasOptions pad; pad.padding = 2;
    AtlasResult p = PackAtlas({{"a",16,16},{"b",16,16}}, pad);
    CHECK(p.ok && Valid(p, 2));
    CHECK(IsPow2(p.width) && IsPow2(p.height));

    // bleed 1: sprite origin is inset by 1 from its cell, cells 18x18 do not overlap
    AtlasOptions bl; bl.bleed = 1;
    AtlasResult b = PackAtlas({{"a",16,16},{"b",16,16}}, bl);
    CHECK(b.ok && Valid(b, 2));       // two bleeds touching: sprites at least 2 apart
    for (auto& q : b.placements) CHECK(q.x >= 1 && q.y >= 1 && q.x + q.w + 1 <= b.width && q.y + q.h + 1 <= b.height);
}

static void TestLimitsAndRotation() {
    RUN_SUITE("tex atlas limits / rotation");
    AtlasOptions lim; lim.maxWidth = 16; lim.maxHeight = 32;
    AtlasResult big = PackAtlas({{"wide",30,10}}, lim);
    CHECK(!big.ok && big.failed.size() == 1 && big.placements.empty());
    lim.allowRotate = true;
    AtlasResult rot = PackAtlas({{"wide",30,10}}, lim);
    CHECK(rot.ok && rot.placements.size() == 1 && rot.placements[0].rotated);
    CHECK(rot.placements[0].w == 10 && rot.placements[0].h == 30);
    CHECK(rot.width <= 16 && rot.height <= 32);

    // invalid items are reported, the rest still pack
    AtlasResult bad = PackAtlas({{"zero",0,5},{"neg",-3,4},{"ok",8,8}});
    CHECK(!bad.ok && bad.failed.size() == 2 && bad.failed[0] == 0 && bad.failed[1] == 1);
    CHECK(bad.placements.size() == 1 && bad.placements[0].item == 2);

    // does not fit at all
    AtlasOptions tiny; tiny.maxWidth = 8; tiny.maxHeight = 8;
    AtlasResult no = PackAtlas({{"a",8,8},{"b",8,8}}, tiny);
    CHECK(!no.ok && no.placements.empty() && no.failed.size() == 2);
}

static void TestRandom() {
    RUN_SUITE("tex atlas random sets");
    unsigned seed = 12345;
    auto rnd = [&](int m) { seed = seed * 1103515245u + 12345u; return int((seed >> 16) % unsigned(m)); };
    for (int round = 0; round < 40; ++round) {
        std::vector<AtlasItem> items;
        int n = 1 + rnd(40);
        for (int i = 0; i < n; ++i) items.push_back({"s" + std::to_string(i), 1 + rnd(48), 1 + rnd(48)});
        AtlasOptions o; o.padding = rnd(3); o.bleed = rnd(2); o.allowRotate = rnd(2) == 1; o.powerOfTwo = rnd(2) == 1;
        AtlasResult r = PackAtlas(items, o);
        CHECK(r.ok);
        CHECK(r.placements.size() == items.size());
        CHECK(Valid(r, o.padding + 2 * o.bleed));
        if (o.powerOfTwo) CHECK(IsPow2(r.width) && IsPow2(r.height));
        // determinism
        AtlasResult r2 = PackAtlas(items, o);
        CHECK(AtlasToJson(r, "x.png", o) == AtlasToJson(r2, "x.png", o));
        for (auto& p : r.placements) {
            bool sw = p.rotated;
            CHECK(p.w == (sw ? items[p.item].h : items[p.item].w));
            if (!o.allowRotate) CHECK(!p.rotated);
        }
    }
}

static void TestComposeAndJson() {
    RUN_SUITE("tex atlas compose / manifest");
    // two sprites: 2x2 red, 2x2 green; bleed 1; compose and look at the bleed ring
    std::uint8_t red[16], green[16];
    for (int i = 0; i < 4; ++i) { red[i*4]=255; red[i*4+1]=0; red[i*4+2]=0; red[i*4+3]=255; green[i*4]=0; green[i*4+1]=255; green[i*4+2]=0; green[i*4+3]=255; }
    std::vector<AtlasItem> items = {{"red",2,2},{"green",2,2}};
    AtlasOptions o; o.bleed = 1; o.padding = 0;
    AtlasResult r = PackAtlas(items, o);
    CHECK(r.ok);
    std::vector<AtlasImage> imgs = {{red,2,2},{green,2,2}};
    auto sheet = ComposeAtlasRGBA8(r, imgs, 1);
    CHECK(sheet.size() == std::size_t(r.width) * r.height * 4);
    for (auto& p : r.placements) {
        const std::uint8_t* want = p.item == 0 ? red : green;
        for (int dy = -1; dy <= p.h; ++dy)
            for (int dx = -1; dx <= p.w; ++dx) {
                const std::uint8_t* d = &sheet[(std::size_t(p.y + dy) * r.width + (p.x + dx)) * 4];
                CHECK(d[0] == want[0] && d[1] == want[1] && d[3] == 255);
            }
    }
    // image size mismatch -> empty
    std::vector<AtlasImage> wrong = {{red,3,3},{green,2,2}};
    CHECK(ComposeAtlasRGBA8(r, wrong, 1).empty());
    CHECK(ComposeAtlasRGBA8(AtlasResult(), imgs, 0).empty());

    // rotation: a 3x1 strip (R,G,B) rotated clockwise reads top to bottom R,G,B in a 1x3 column
    std::uint8_t strip[12] = {255,0,0,255, 0,255,0,255, 0,0,255,255};
    AtlasOptions ro; ro.maxWidth = 1; ro.maxHeight = 4; ro.allowRotate = true;
    AtlasResult rr = PackAtlas({{"strip",3,1}}, ro);
    CHECK(rr.ok && rr.placements[0].rotated && rr.placements[0].w == 1 && rr.placements[0].h == 3);
    auto rs = ComposeAtlasRGBA8(rr, {{strip,3,1}}, 0);
    CHECK(!rs.empty());
    int px = rr.placements[0].x, py = rr.placements[0].y;
    // clockwise turn of a left-to-right strip puts the first texel at the top
    CHECK(rs[(std::size_t(py + 0) * rr.width + px) * 4] == 255);
    CHECK(rs[(std::size_t(py + 1) * rr.width + px) * 4 + 1] == 255);
    CHECK(rs[(std::size_t(py + 2) * rr.width + px) * 4 + 2] == 255);

    // exact manifest text
    AtlasOptions jo; jo.powerOfTwo = false;
    AtlasResult jr = PackAtlas({{"b\"x",4,4},{"a",4,4}}, jo);
    std::string js = AtlasToJson(jr, "sheet.png", jo);
    CHECK(js.find("{\n  \"image\": \"sheet.png\",\n  \"width\": 8,\n  \"height\": 4,\n  \"padding\": 0,\n  \"bleed\": 0,\n  \"sprites\": [\n    {\"name\": \"a\"") == 0);
    CHECK(js.find("{\"name\": \"b\\\"x\"") != std::string::npos);
    CHECK(js.find("\"rotated\": false}") != std::string::npos);
    CHECK(js.back() == '\n');
    std::string empty = AtlasToJson(AtlasResult(), "e.png", AtlasOptions());
    CHECK(empty == "{\n  \"image\": \"e.png\",\n  \"width\": 0,\n  \"height\": 0,\n  \"padding\": 0,\n  \"bleed\": 0,\n  \"sprites\": []\n}\n");
}

int main() {
    TestExact();
    TestLimitsAndRotation();
    TestRandom();
    TestComposeAndJson();
    TEST_MAIN_RESULT();
}
