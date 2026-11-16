// Environment templates for guided modeling: tree, rock, road, bridge, hedge, plant and street lamp. Each is a set of named
// parts that can be given another design; random-looking variation comes from a seed setting and is deterministic. Written from
// general parametric modeling (stacked cones and ellipsoids, displaced vertices, strips along a path, arches as extruded
// profiles); no code, models or assets from other projects. Standing on y = 0, centred on x = z = 0 unless it follows a path.
#include "GuidedKit.hpp"
#include <map>

namespace okay::modeling::guided {
namespace {
using namespace kit;
const Vec3 kUp{0, 1, 0};

// local (x along, y up, z across) -> o + t*x + up*(y + rise*x) + n*z, keeping faces outward
void PlaceSheared(PolyMesh& m, Vec3 o, Vec3 t, Vec3 n, float rise) {
    for (Vec3& v : m.verts) v = o + t * v.x + kUp * (v.y + rise * v.x) + n * v.z;
    if (Vec3::Dot(Vec3::Cross(t, kUp), n) < 0)
        for (PolyFace& f : m.faces) { std::reverse(f.verts.begin(), f.verts.end()); std::reverse(f.uvs.begin(), f.uvs.end()); }
}

// ---- Tree ------------------------------------------------------------------------------------------------------
void GenerateTree(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float H = Pf(r, "height"), tr = Pf(r, "trunkRadius"), cs = Pf(r, "crownSize"), lean = Pf(r, "lean");
    Rnd rnd((uint32_t)r.Param("seed"));
    const std::string crown = r.DesignFor("crown", "crown/0"), trunk = r.DesignFor("trunk", "trunk/0");
    const float topFrac = crown == "pine" ? 0.4f : crown == "spruce" ? 0.35f : crown == "palm" ? 0.95f : crown == "dead" || crown == "none" ? 1.0f : crown == "willow" ? 0.55f : 0.62f;
    const float top = H * topFrac;
    auto bend = [&](float t) { return lean * H * 0.12f * t * t; };   // sideways lean grows towards the top
    // trunk: stacked prisms of decreasing radius (a curve when it leans)
    {
        const bool forked = trunk == "forked", thick = trunk == "thick", curved = trunk == "curved";
        Frame f = FrameAA({-tr * 2, 0, -tr * 2}, {tr * 2 + bend(1), top, tr * 2});
        o.Add("trunk/0", "trunk", f, [&](const std::string&) {
            PolyMesh m;
            const int seg = 5;
            const float stop = forked ? top * 0.55f : top;
            for (int k = 0; k < seg; ++k) {
                const float t0 = (float)k / seg, t1 = (float)(k + 1) / seg;
                const float rad = tr * (1.0f - 0.55f * t0) * (thick && k == 0 ? 1.6f : 1.0f) * (1.0f + 0.01f * (float)k);
                const float bx0 = (curved || lean > 0) ? bend(t0) : 0, bx1 = (curved || lean > 0) ? bend(t1) : 0;
                shapes::Append(m, Limb({bx0, stop * t0 - (k ? 0.02f : 0), 0}, {bx1, stop * t1, 0}, rad, true, 8));
            }
            if (forked) for (int s = -1; s <= 1; s += 2) shapes::Append(m, Limb({bend(0.55f), stop, 0}, {bend(0.55f) + s * cs * 0.45f, top, s * 0.1f}, tr * 0.5f, true, 8));
            return m;
        });
    }
    const Vec3 cc{bend(1), crown == "pine" || crown == "spruce" ? top : top + cs * 0.55f, 0};
    // crown
    {
        Frame f = FrameAA({cc.x - cs, top * 0.5f, -cs}, {cc.x + cs, top + cs * 1.5f, cs});
        o.Add("crown/0", "crown", f, [&](const std::string& d) {
            PolyMesh m;
            if (d == "pine" || d == "spruce") {
                const int n = d == "pine" ? 5 : 7;
                const float step = (H - top * 0.6f) / (float)n, w = d == "pine" ? cs : cs * 0.7f;
                for (int k = 0; k < n; ++k) {
                    const float y0 = top * 0.6f + step * (float)k, rad = w * (1.0f - 0.78f * (float)k / (float)n);
                    shapes::Append(m, Cone({cc.x, y0, 0}, {cc.x, y0 + step * 1.7f, 0}, rad, 8));
                }
                return m;
            }
            if (d == "palm") {
                const int n = 8;
                for (int k = 0; k < n; ++k) {
                    const float a = 2 * kPi * (float)k / (float)n + rnd.R(0, 0.2f);
                    const Vec3 a0{cc.x, top, 0}, a1{cc.x + std::cos(a) * cs * 1.3f, top - cs * 0.45f, std::sin(a) * cs * 1.3f};
                    Frame bf = AxisFrame(a0, a1, cs * 0.3f, 0.03f);
                    bf.t = Vec3::Cross(bf.u, kUp).Normalized(); bf.n = Vec3::Cross(bf.t, bf.u);
                    shapes::Append(m, shapes::Box(bf));
                }
                for (int k = 0; k < 3; ++k) shapes::Append(m, Ell({cc.x + (float)(k - 1) * 0.1f, top - 0.1f - 0.03f * (float)k, 0.1f * (float)(k % 2 ? 1 : -1)}, 0.09f, 0.09f, 0.09f, 4, 6));
                return m;
            }
            if (d == "dead") return m;
            const int n = d == "cluster" ? 9 : d == "willow" ? 4 : 5;
            for (int k = 0; k < n; ++k) {
                const float spread = d == "cluster" ? 0.7f : 0.45f;
                const float rad = cs * (d == "cluster" ? rnd.R(0.3f, 0.42f) : rnd.R(0.5f, 0.72f));
                const Vec3 p{cc.x + rnd.R(-1, 1) * cs * spread, cc.y + rnd.R(-0.5f, 0.6f) * cs * spread, rnd.R(-1, 1) * cs * spread};
                shapes::Append(m, Ell(p, rad, rad * 0.85f, rad));
            }
            if (d == "willow") for (int k = 0; k < 10; ++k) {
                const float a = 2 * kPi * (float)k / 10.0f;
                const Vec3 p{cc.x + std::cos(a) * cs * 0.85f, cc.y - cs * 0.2f, std::sin(a) * cs * 0.85f};
                shapes::Append(m, BoxAA({p.x - 0.025f, p.y - cs * 1.2f, p.z - 0.025f}, {p.x + 0.025f, p.y, p.z + 0.025f}));
            }
            return m;
        });
    }
    // branches
    {
        const std::string bd = r.DesignFor("branch", "branch/0");
        const int n = bd == "many" ? 8 : bd == "few" ? 4 : 0;
        if (n) {
            Frame f = FrameAA({-cs, top * 0.4f, -cs}, {cs, top + 0.5f, cs});
            o.Add("branch/0", "branch", f, [&](const std::string&) {
                PolyMesh m;
                for (int k = 0; k < n; ++k) {
                    const float a = 2 * kPi * (float)k / (float)n + 0.4f, t = 0.45f + 0.5f * (float)k / (float)n;
                    const Vec3 b0{bend(t), top * t, 0}, b1{b0.x + std::cos(a) * cs * 0.9f, b0.y + cs * rnd.R(0.3f, 0.7f), std::sin(a) * cs * 0.9f};
                    shapes::Append(m, Limb(b0, b1, tr * 0.3f, true, 6));
                }
                return m;
            });
        }
    }
    // roots, fruit
    o.Add("roots/0", "roots", FrameAA({-tr * 3, 0, -tr * 3}, {tr * 3, 0.3f, tr * 3}), [&](const std::string&) {
        PolyMesh m;
        for (int k = 0; k < 5; ++k) { const float a = 2 * kPi * (float)k / 5.0f + 0.3f; shapes::Append(m, Cone({std::cos(a) * tr * 0.5f, 0.3f, std::sin(a) * tr * 0.5f}, {std::cos(a) * tr * 3.0f, 0.0f, std::sin(a) * tr * 3.0f}, tr * 0.42f, 5)); }
        return m;
    });
    {
        Rnd fr(7 + (uint32_t)r.Param("seed"));
        o.Add("fruit/0", "fruit", FrameAA({cc.x - cs, cc.y - cs, -cs}, {cc.x + cs, cc.y + cs, cs}), [&](const std::string& d) {
            PolyMesh m;
            const float rad = d == "blossom" ? 0.05f : 0.08f;
            for (int k = 0; k < 14; ++k) {
                const float a = fr.R(0, 2 * kPi), b = fr.R(-0.4f, 1.0f);
                shapes::Append(m, Ell({cc.x + std::cos(a) * cs * 0.8f, cc.y + b * cs * 0.6f, std::sin(a) * cs * 0.8f}, rad, rad, rad, 4, 6));
            }
            return m;
        });
    }
}

Template MakeTree() {
    B b;
    b.t.id = "tree"; b.t.name = "Tree"; b.t.category = "Environment"; b.t.version = 1;
    b.t.description = "A tree: trunk, crown, branches, roots and fruit, in many species. The seed changes the shape without changing the species.";
    b.P("height", "Height", 6, 1.5, 25, "Size", "Overall height.");
    b.P("trunkRadius", "Trunk radius", 0.25, 0.04, 1.2, "Size", "Radius of the trunk at the base.");
    b.P("crownSize", "Crown size", 2.0, 0.4, 8, "Size", "Radius of the crown.");
    b.P("lean", "Lean", 0.0, -1, 1, "Size", "How far the tree leans to one side.");
    b.P("seed", "Variation (seed)", 1, 0, 999, "Size", "Another number makes another tree of the same kind.", B::K::Int);
    const Color kBark{0.38f, 0.26f, 0.16f, 1}, kLeaf{0.2f, 0.5f, 0.2f, 1}, kPine{0.12f, 0.35f, 0.2f, 1}, kBirch{0.86f, 0.86f, 0.82f, 1}, kRed{0.75f, 0.12f, 0.1f, 1}, kWhite{0.95f, 0.9f, 0.92f, 1};
    b.S("trunk", "Trunk", {{"straight", "Straight"}, {"curved", "Curved"}, {"thick", "Thick base"}, {"forked", "Forked"}}, kBark, 0);
    b.S("crown", "Crown", {{"round", "Round"}, {"cluster", "Clusters"}, {"pine", "Pine"}, {"spruce", "Spruce"}, {"palm", "Palm fronds"}, {"willow", "Willow"}, {"dead", "Bare"}}, kLeaf, 1);
    b.S("branch", "Branches", {{"none", "None"}, {"few", "A few"}, {"many", "Many"}}, kBark, 2);
    b.S("roots", "Roots", {{"none", "None"}, {"flare", "Flared roots"}}, kBark, 3);
    b.S("fruit", "Fruit", {{"none", "None"}, {"apples", "Apples"}, {"blossom", "Blossom"}}, kRed, 4);
    b.St("oak", "Oak", "A round crown on a thick trunk.", {{"height", 7}, {"trunkRadius", 0.35}, {"crownSize", 2.6}}, {{"trunk", "thick"}, {"crown", "round"}, {"branch", "few"}, {"roots", "flare"}}, {});
    b.St("pine", "Pine", "Stacked cones on a slim trunk.", {{"height", 9}, {"trunkRadius", 0.22}, {"crownSize", 1.8}}, {{"trunk", "straight"}, {"crown", "pine"}}, {{"crown", kPine}});
    b.St("spruce", "Spruce", "A tall narrow fir shape.", {{"height", 11}, {"trunkRadius", 0.2}, {"crownSize", 1.7}}, {{"trunk", "straight"}, {"crown", "spruce"}}, {{"crown", kPine}});
    b.St("birch", "Birch", "A slim pale trunk with small leaf clusters.", {{"height", 7.5}, {"trunkRadius", 0.14}, {"crownSize", 1.9}, {"lean", 0.2}}, {{"trunk", "curved"}, {"crown", "cluster"}, {"branch", "few"}}, {{"trunk", kBirch}, {"branch", kBirch}});
    b.St("palm", "Palm", "A tall leaning trunk with fronds.", {{"height", 8}, {"trunkRadius", 0.2}, {"crownSize", 2.2}, {"lean", 0.7}}, {{"trunk", "curved"}, {"crown", "palm"}}, {{"trunk", Color{0.55f, 0.42f, 0.28f, 1}}, {"crown", Color{0.25f, 0.6f, 0.25f, 1}}});
    b.St("willow", "Willow", "Hanging strands around a leafy crown.", {{"height", 6.5}, {"trunkRadius", 0.3}, {"crownSize", 2.4}, {"lean", 0.3}}, {{"trunk", "forked"}, {"crown", "willow"}, {"roots", "flare"}}, {{"crown", Color{0.45f, 0.65f, 0.3f, 1}}});
    b.St("dead", "Dead tree", "Bare branches and a forked trunk.", {{"height", 6}, {"trunkRadius", 0.22}, {"crownSize", 2.0}}, {{"trunk", "forked"}, {"crown", "dead"}, {"branch", "many"}, {"roots", "flare"}}, {{"trunk", Color{0.3f, 0.25f, 0.22f, 1}}, {"branch", Color{0.3f, 0.25f, 0.22f, 1}}});
    b.St("apple", "Apple tree", "A round crown with apples.", {{"height", 4.5}, {"trunkRadius", 0.22}, {"crownSize", 1.9}}, {{"trunk", "curved"}, {"crown", "round"}, {"branch", "few"}, {"fruit", "apples"}}, {});
    b.St("blossom", "Blossom tree", "A pink-white crown full of blossom.", {{"height", 4.2}, {"trunkRadius", 0.2}, {"crownSize", 2.0}, {"seed", 9}}, {{"trunk", "forked"}, {"crown", "cluster"}, {"fruit", "blossom"}}, {{"crown", Color{0.95f, 0.7f, 0.78f, 1}}, {"fruit", kWhite}});
    b.t.generate = GenerateTree;
    return b.t;
}

// ---- Rock ------------------------------------------------------------------------------------------------------
// Roughen a mesh: every vertex moves along the direction from `c` by a deterministic amount (topology unchanged).
void Roughen(PolyMesh& m, Vec3 c, float amount, uint32_t seed) {
    for (Vec3& v : m.verts) {
        uint32_t h = seed * 374761393u + (uint32_t)std::lround(v.x * 40) * 668265263u + (uint32_t)std::lround(v.y * 40) * 2246822519u + (uint32_t)std::lround(v.z * 40) * 3266489917u;
        h = (h ^ (h >> 13)) * 1274126177u;
        const float n = (float)((h >> 8) & 0xffff) / 65535.0f * 2.0f - 1.0f;
        v = c + (v - c) * (1.0f + n * amount);
    }
}

void GenerateRock(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float size = Pf(r, "size"), spread = Pf(r, "spread"), rough = Pf(r, "roughness"), flat = Pf(r, "flatten");
    const int count = std::max(1, (int)r.Param("count"));
    const uint32_t seed = (uint32_t)r.Param("seed");
    Rnd rnd(seed);
    for (int i = 0; i < count; ++i) {
        const float sz = size * (i == 0 ? 1.0f : rnd.R(0.35f, 0.8f));
        const float a = rnd.R(0, 2 * kPi), d = i == 0 ? 0.0f : spread * rnd.R(0.4f, 1.0f);
        const Vec3 c{std::cos(a) * d + 0.0013f * (float)i, 0, std::sin(a) * d};
        const std::string id = "rock/" + std::to_string(i);
        Frame f = FrameAA({c.x - sz, -sz * 0.3f, c.z - sz}, {c.x + sz, sz * flat * 1.4f, c.z + sz});
        o.Add(id, "rock", f, [&](const std::string& dsg) {
            const float ry = sz * flat * 0.7f;
            PolyMesh m;
            Vec3 centre{c.x, ry * 0.6f, c.z};
            if (dsg == "columns") {
                const int n = 7;
                const float rad = sz * 0.32f;
                for (int k = 0; k < n; ++k) {
                    const float ox = k == 0 ? 0.0f : std::cos(kPi / 3.0f * (float)(k - 1)) * rad * 1.8f, oz = k == 0 ? 0.0f : std::sin(kPi / 3.0f * (float)(k - 1)) * rad * 1.8f;
                    Frame pf; pf.origin = {c.x + ox, -0.1f, c.z + oz}; pf.t = {1, 0, 0}; pf.u = {0, 1, 0}; pf.n = {0, 0, 1};
                    pf.sx = pf.sz = rad * 2; pf.sy = sz * flat * rnd.R(0.9f, 2.0f) + 0.1f;
                    shapes::Append(m, shapes::Prism(pf, 6));
                }
                return m;
            }
            if (dsg == "spike") { m = Cone({c.x, -sz * 0.2f, c.z}, {c.x + rnd.R(-0.1f, 0.1f) * sz, sz * flat * 2.0f, c.z}, sz * 0.7f, 5); Roughen(m, {c.x, sz * 0.5f, c.z}, rough * 0.6f, seed + i); return m; }
            if (dsg == "boxy") { m = BoxAA({c.x - sz * 0.8f, -sz * 0.1f, c.z - sz * 0.7f}, {c.x + sz * 0.8f, sz * flat * 1.1f, c.z + sz * 0.7f}); Roughen(m, centre, rough * 0.7f, seed + i); return m; }
            m = Ell(centre, sz, ry, sz * 0.9f, 6, 10);
            Roughen(m, centre, rough, seed + i);
            return m;
        });
        // moss on top
        o.Add("moss/" + std::to_string(i), "moss", FrameAA({c.x - sz * 0.6f, sz * flat * 0.8f, c.z - sz * 0.6f}, {c.x + sz * 0.6f, sz * flat * 1.2f, c.z + sz * 0.6f}), [&](const std::string&) {
            return Ell({c.x, sz * flat * 0.9f, c.z}, sz * 0.6f, sz * flat * 0.18f, sz * 0.55f, 4, 8);
        });
    }
}

Template MakeRock() {
    B b;
    b.t.id = "rock"; b.t.name = "Rock"; b.t.category = "Environment"; b.t.version = 1;
    b.t.description = "Rocks: one boulder or a pile of them, spikes, basalt columns, with moss. Roughness and the seed make each one different.";
    b.P("size", "Size", 1.0, 0.1, 6, "Size", "Size of the largest rock.");
    b.P("count", "How many", 1, 1, 14, "Size", "Number of rocks in the pile.", B::K::Int);
    b.P("spread", "Spread", 1.5, 0.2, 8, "Size", "How far the smaller rocks lie from the first.");
    b.P("roughness", "Roughness", 0.18, 0, 0.5, "Shape", "How lumpy the surface is.");
    b.P("flatten", "Height", 0.8, 0.2, 2.5, "Shape", "How tall the rocks are compared with their width.");
    b.P("seed", "Variation (seed)", 1, 0, 999, "Shape", "Another number makes another set of rocks.", B::K::Int);
    const Color kGrey{0.5f, 0.5f, 0.5f, 1}, kMoss{0.25f, 0.45f, 0.2f, 1};
    b.S("rock", "Rock shape", {{"boulder", "Boulder"}, {"boxy", "Chunky block"}, {"spike", "Spike"}, {"columns", "Basalt columns"}}, kGrey, 0);
    b.S("moss", "Moss", {{"none", "None"}, {"cap", "Moss cap"}}, kMoss, 1);
    b.St("boulder", "Boulder", "One big rounded rock.", {{"size", 1.4}, {"count", 1}}, {{"rock", "boulder"}}, {});
    b.St("pile", "Rock pile", "A big rock with smaller ones around it.", {{"count", 6}, {"size", 1.2}, {"seed", 4}}, {{"rock", "boulder"}}, {});
    b.St("flat_stones", "Flat stones", "Low wide stones.", {{"count", 5}, {"flatten", 0.3}, {"size", 1.0}, {"spread", 2.0}, {"seed", 6}}, {{"rock", "boxy"}}, {{"rock", Color{0.6f, 0.58f, 0.52f, 1}}});
    b.St("spikes", "Rock spikes", "Sharp upright rocks.", {{"count", 5}, {"flatten", 1.2}, {"size", 0.7}, {"roughness", 0.25}, {"seed", 3}}, {{"rock", "spike"}}, {{"rock", Color{0.4f, 0.4f, 0.45f, 1}}});
    b.St("basalt", "Basalt columns", "Hexagonal columns of different heights.", {{"count", 3}, {"size", 1.2}, {"flatten", 1.0}, {"spread", 2.6}, {"seed", 2}}, {{"rock", "columns"}}, {{"rock", Color{0.3f, 0.3f, 0.33f, 1}}});
    b.St("standing", "Standing stone", "A tall single stone.", {{"count", 1}, {"flatten", 2.4}, {"size", 0.7}, {"roughness", 0.12}}, {{"rock", "boxy"}}, {{"rock", Color{0.55f, 0.55f, 0.5f, 1}}});
    b.St("pebbles", "Pebbles", "Many small stones.", {{"count", 14}, {"size", 0.2}, {"spread", 1.0}, {"flatten", 0.6}, {"seed", 8}}, {{"rock", "boulder"}}, {{"rock", Color{0.62f, 0.6f, 0.55f, 1}}});
    b.St("mossy", "Mossy rocks", "A pile of rocks with green caps.", {{"count", 4}, {"size", 1.1}, {"seed", 5}}, {{"rock", "boulder"}, {"moss", "cap"}}, {});
    b.St("cliff_stack", "Rock stack", "Chunky blocks piled up.", {{"count", 6}, {"size", 1.3}, {"flatten", 1.1}, {"spread", 1.2}, {"seed", 12}}, {{"rock", "boxy"}}, {{"rock", Color{0.5f, 0.45f, 0.4f, 1}}});
    b.t.generate = GenerateRock;
    return b.t;
}

// ---- Road, path and railway --------------------------------------------------------------------------------------
void GenerateRoad(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float W = Pf(r, "width"), th = Pf(r, "thickness"), kh = Pf(r, "kerbHeight");
    std::vector<Vec3> pts = r.path;
    if (pts.size() < 2) pts = {{0, 0, 0}, {Pf(r, "length"), 0, 0}};
    std::vector<float> cum(1, 0.0f);
    for (size_t i = 1; i < pts.size(); ++i) { const Vec3 d = pts[i] - pts[i - 1]; cum.push_back(cum.back() + std::sqrt(d.x * d.x + d.z * d.z)); }
    const int segs = (int)pts.size() - 1;
    for (int si = 0; si < segs; ++si) {
        const Vec3 a = pts[si], b2 = pts[si + 1];
        const float len = cum[si + 1] - cum[si];
        if (len < 0.2f) continue;
        const Vec3 t = Vec3{b2.x - a.x, 0, b2.z - a.z} / len;
        const Vec3 n = Vec3::Cross(t, kUp);
        const float rise = (b2.y - a.y) / len;
        const float e0 = si > 0 ? W * 0.5f - 0.004f : 0.0f, e1 = si + 1 < segs ? W * 0.5f - 0.004f : 0.0f;   // butt joints at corners
        const float L = len + e0 + e1;
        const std::string sd = std::to_string(si);
        const Vec3 org = a - t * e0 - kUp * 0.0f;
        const float org0 = e0;
        (void)org0;
        auto place = [&](PolyMesh m, float xShift) { for (Vec3& v : m.verts) v.x += xShift; PlaceSheared(m, a - t * e0, t, n, rise); return m; };
        (void)org;
        const std::string sdsn = r.DesignFor("surface", "road/" + sd);
        Frame fs = FrameAA({0, 0, -W / 2}, {L, th, W / 2});
        o.Add("road/" + sd, "surface", fs, [&](const std::string& d) {
            PolyMesh m;
            if (d == "railway") {
                shapes::Append(m, BoxAA({0, 0, -W * 0.3f}, {L, th, W * 0.3f}));   // ballast
                const int ns = std::max(2, (int)(L / 0.6f));
                for (int k = 0; k < ns; ++k) { const float x = (L - 0.2f) * ((float)k + 0.5f) / (float)ns; shapes::Append(m, BoxAA({x, th - 0.01f, -W * 0.42f}, {x + 0.2f, th + 0.1f, W * 0.42f})); }
                for (int s = -1; s <= 1; s += 2) shapes::Append(m, BoxAA({0.01f, th + 0.09f, s * W * 0.2f - 0.035f}, {L - 0.01f, th + 0.2f, s * W * 0.2f + 0.035f}));
                return place(m, 0);
            }
            if (d == "cobble" || d == "paving") {
                shapes::Append(m, BoxAA({0, 0, -W / 2}, {L, th, W / 2}));
                const float cell = d == "cobble" ? 0.5f : 1.0f;
                const int nx = std::max(1, (int)(L / cell)), nz = std::max(1, (int)(W / cell));
                for (int ix = 0; ix < nx; ++ix) for (int iz = 0; iz < nz; ++iz) {
                    const float x0 = L * (float)ix / (float)nx, x1 = L * (float)(ix + 1) / (float)nx, z0 = -W / 2 + W * (float)iz / (float)nz, z1 = -W / 2 + W * (float)(iz + 1) / (float)nz;
                    shapes::Append(m, BoxAA({x0 + 0.03f, th - 0.01f, z0 + 0.03f}, {x1 - 0.03f, th + (d == "cobble" ? 0.05f : 0.02f), z1 - 0.03f}));
                }
                return place(m, 0);
            }
            if (d == "gravel") {
                shapes::Append(m, BoxAA({0, 0, -W / 2}, {L, th, W / 2}));
                Rnd rr(11 + (uint32_t)si);
                const int nb = std::max(4, (int)(L * W * 2.5f));
                for (int k = 0; k < nb; ++k) shapes::Append(m, Ell({rr.R(0.1f, L - 0.1f), th, rr.R(-W / 2 + 0.1f, W / 2 - 0.1f)}, 0.07f, 0.03f, 0.07f, 3, 5));
                return place(m, 0);
            }
            return place(BoxAA({0, 0, -W / 2}, {L, d == "dirt" ? th * 0.7f : th, W / 2}), 0);   // asphalt, concrete, dirt
        });
        (void)sdsn;
        // markings on top
        const std::string mk = r.DesignFor("marking", "marking/" + sd + "/0");
        if (mk != "none" && !mk.empty() && r.DesignFor("surface", "road/" + sd) != "railway") {
            PolyMesh m;
            const float y0 = th - 0.005f, y1 = th + 0.008f;
            if (mk == "dashed") for (float x = 0.5f + e0; x + 1.5f < len - 0.3f + e0; x += 4.5f) shapes::Append(m, BoxAA({x, y0, -0.06f}, {x + 1.5f, y1, 0.06f}));
            else if (mk == "double") for (int s = -1; s <= 1; s += 2) shapes::Append(m, BoxAA({0.4f + e0, y0, s * 0.12f - 0.04f}, {len - 0.4f + e0, y1, s * 0.12f + 0.04f}));
            else if (mk == "edges") for (int s = -1; s <= 1; s += 2) shapes::Append(m, BoxAA({0.4f + e0, y0, s * (W / 2 - 0.3f) - 0.05f}, {len - 0.4f + e0, y1, s * (W / 2 - 0.3f) + 0.05f}));
            if (!m.faces.empty()) {
                Frame fm = FrameAA({0, y0, -W / 2}, {L, y1, W / 2});
                o.Add("marking/" + sd + "/0", "marking", fm, [&](const std::string&) { return place(m, 0); });
            }
        }
        for (int side = 0; side < 2; ++side) {
            const float s = side ? 1.0f : -1.0f;
            const std::string kid = "kerb/" + sd + "/" + std::to_string(side), wid = "walk/" + sd + "/" + std::to_string(side);
            const float in0 = 0.05f + e0, in1 = len - 0.05f + e0;
            o.Add(kid, "kerb", FrameAA({in0, 0, s > 0 ? W / 2 - 0.12f : -W / 2 - 0.08f}, {in1, th + kh, s > 0 ? W / 2 + 0.08f : -W / 2 + 0.12f}), [&](const std::string& d) {
                PolyMesh m;
                if (d == "barrier") m = shapes::ExtrudeAxes({(in1 - in0) * 0.5f, 0, 0}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0}, {{s * (W / 2 - 0.15f) - 0.25f, 0}, {s * (W / 2 - 0.15f) + 0.25f, 0}, {s * (W / 2 - 0.15f) + 0.1f, th + 0.8f}, {s * (W / 2 - 0.15f) - 0.1f, th + 0.8f}}, in1 - in0);
                else if (d == "wall") m = BoxAA({0, 0, s * (W / 2 + 0.1f) - 0.12f}, {in1 - in0, th + 0.7f, s * (W / 2 + 0.1f) + 0.12f});
                else m = BoxAA({0, 0, s > 0 ? W / 2 - 0.12f : -W / 2 - 0.08f}, {in1 - in0, th + kh, s > 0 ? W / 2 + 0.08f : -W / 2 + 0.12f});
                return place(m, in0);
            });
            o.Add(wid, "walk", FrameAA({in0, 0, s > 0 ? W / 2 + 0.1f : -W / 2 - 1.6f}, {in1, kh * 0.8f, s > 0 ? W / 2 + 1.6f : -W / 2 - 0.1f}), [&](const std::string&) {
                PolyMesh m = BoxAA({0, 0, s > 0 ? W / 2 + 0.12f : -W / 2 - 1.6f - 0.02f}, {in1 - in0, kh * 0.8f, s > 0 ? W / 2 + 1.62f : -W / 2 - 0.12f});
                return place(m, in0);
            });
        }
    }
}

Template MakeRoad() {
    B b;
    b.t.id = "road"; b.t.name = "Road"; b.t.category = "Environment"; b.t.version = 1;
    b.t.usesPath = true;
    b.t.description = "A road, path or railway along a straight run or a path you draw (Draw in the Scene view): surface, markings, kerbs and pavements. Corners are butt-joined.";
    b.P("length", "Length", 20, 2, 300, "Size", "Length of a straight road (ignored when you draw a path).");
    b.P("width", "Width", 6, 0.6, 20, "Size", "Width of the road surface.");
    b.P("thickness", "Thickness", 0.15, 0.04, 0.6, "Size", "Thickness of the surface.");
    b.P("kerbHeight", "Kerb height", 0.15, 0.03, 0.5, "Size", "How far kerbs and pavements rise above the surface.");
    const Color kAsph{0.2f, 0.2f, 0.22f, 1}, kLine{0.92f, 0.9f, 0.8f, 1}, kKerb{0.62f, 0.62f, 0.6f, 1}, kWalk{0.7f, 0.68f, 0.64f, 1};
    b.S("surface", "Surface", {{"asphalt", "Asphalt"}, {"concrete", "Concrete"}, {"dirt", "Dirt"}, {"gravel", "Gravel"}, {"cobble", "Cobblestones"}, {"paving", "Paving slabs"}, {"railway", "Railway"}}, kAsph, 0);
    b.S("marking", "Markings", {{"none", "None"}, {"dashed", "Dashed centre line"}, {"double", "Double centre line"}, {"edges", "Edge lines"}}, kLine, 1);
    b.S("kerb", "Edges", {{"none", "None"}, {"kerb", "Kerb"}, {"barrier", "Barrier"}, {"wall", "Low wall"}}, kKerb, 2);
    b.S("walk", "Pavement", {{"none", "None"}, {"paved", "Pavement"}}, kWalk, 3);
    b.St("street", "Street", "Asphalt with a dashed line, kerbs and pavements.", {{"width", 6}}, {{"surface", "asphalt"}, {"marking", "dashed"}, {"kerb", "kerb"}, {"walk", "paved"}}, {});
    b.St("country", "Country road", "A narrow road with edge lines.", {{"width", 4.5}}, {{"surface", "asphalt"}, {"marking", "edges"}}, {});
    b.St("dirt_path", "Dirt path", "A narrow trodden track.", {{"width", 1.6}, {"thickness", 0.08}}, {{"surface", "dirt"}}, {{"surface", Color{0.5f, 0.38f, 0.25f, 1}}});
    b.St("cobble", "Cobblestone street", "Paved with small stones.", {{"width", 5}, {"length", 12}}, {{"surface", "cobble"}, {"kerb", "kerb"}}, {{"surface", Color{0.5f, 0.48f, 0.45f, 1}}});
    b.St("highway", "Highway", "Wide with a double line and crash barriers.", {{"width", 14}}, {{"surface", "asphalt"}, {"marking", "double"}, {"kerb", "barrier"}}, {{"kerb", kKerb}});
    b.St("footpath", "Footpath", "Paving slabs.", {{"width", 1.6}, {"thickness", 0.08}}, {{"surface", "paving"}}, {{"surface", Color{0.65f, 0.63f, 0.6f, 1}}});
    b.St("railway", "Railway", "Ballast, sleepers and two rails.", {{"width", 2.6}, {"thickness", 0.25}}, {{"surface", "railway"}}, {{"surface", Color{0.45f, 0.42f, 0.4f, 1}}});
    b.St("gravel_drive", "Gravel drive", "A loose gravel track with low walls.", {{"width", 3}, {"thickness", 0.1}}, {{"surface", "gravel"}, {"kerb", "wall"}}, {{"surface", Color{0.6f, 0.55f, 0.45f, 1}}});
    b.t.generate = GenerateRoad;
    return b.t;
}

// ---- Bridge ----------------------------------------------------------------------------------------------------
void GenerateBridge(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float L = Pf(r, "length"), W = Pf(r, "width"), h = Pf(r, "height"), dt = Pf(r, "deckThickness");
    const int piers = std::max(1, (int)r.Param("supports"));
    const std::string st = r.DesignFor("structure", "structure/0"), deck = r.DesignFor("deck", "deck/0"), rail = r.DesignFor("rail", "rail/0");
    const float x0 = -L / 2, x1 = L / 2, dy1 = h, dy0 = h - dt;
    // deck
    o.Add("deck/0", "deck", FrameAA({x0, dy0, -W / 2}, {x1, dy1, W / 2}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "planks") { const int n = std::max(3, (int)(L / 0.25f)); for (int k = 0; k < n; ++k) { const float a = x0 + L * (float)k / (float)n, b2 = x0 + L * (float)(k + 1) / (float)n; shapes::Append(m, BoxAA({a + 0.012f, dy0, -W / 2}, {b2 - 0.012f, dy1, W / 2})); } return m; }
        if (d == "logs") { const int n = std::max(3, (int)(L / 0.3f)); for (int k = 0; k < n; ++k) { const float x = x0 + L * ((float)k + 0.5f) / (float)n; Frame f = FrameAA({x - 0.13f, dy0 - 0.05f, -W / 2}, {x + 0.13f, dy1, W / 2}); f.sy = W; Frame g; g.origin = {x, (dy0 + dy1) / 2 - 0.0f, -W / 2}; g.t = {0, 1, 0}; g.u = {0, 0, 1}; g.n = {1, 0, 0}; g.sx = g.sz = std::max(dt + 0.05f, 0.2f); g.sy = W; shapes::Append(m, shapes::Prism(g, 8)); } return m; }
        if (d == "stone") return BoxAA({x0, dy0 - 0.15f, -W / 2 - 0.1f}, {x1, dy1, W / 2 + 0.1f});
        return BoxAA({x0, dy0, -W / 2}, {x1, dy1, W / 2});
    });
    // structure
    o.Add("structure/0", "structure", FrameAA({x0, -0.5f, -W / 2}, {x1, dy1 + 6, W / 2}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "arch") {
            const float R = std::min(L * 0.38f, h * 0.9f + L * 0.1f), top = dy0 + 0.02f;
            std::vector<Vec2> prof = {{x0, 0}, {-R, 0}};
            const int seg = shapes::Scale(10, 4);
            for (int k = 1; k < seg; ++k) { const float a = kPi - kPi * (float)k / (float)seg; prof.push_back({std::cos(a) * R, std::sin(a) * R * std::min(1.0f, (top - 0.3f) / R)}); }
            prof.push_back({R, 0}); prof.push_back({x1, 0}); prof.push_back({x1, top}); prof.push_back({x0, top});
            return shapes::ExtrudeAxes({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, prof, W * 0.9f);
        }
        if (d == "suspension") {
            for (int s = -1; s <= 1; s += 2) {
                const float z = s * (W / 2 + 0.15f), tx = x0 + L * 0.08f, ty = x1 - L * 0.08f, th2 = dy1 + std::max(2.5f, L * 0.18f);
                for (float tower : {tx, ty}) shapes::Append(m, BoxAA({tower - 0.18f, dy0 - 0.4f, z - 0.15f}, {tower + 0.18f, th2, z + 0.15f}));
                const int segs = 10;
                Vec3 prev{tx, th2, z};
                for (int k = 1; k <= segs; ++k) {
                    const float u = (float)k / (float)segs, x = tx + (ty - tx) * u, y = dy1 + 0.5f + (th2 - dy1 - 0.5f) * (2 * u - 1) * (2 * u - 1);
                    const Vec3 p{x, y, z};
                    shapes::Append(m, Limb(prev, p, 0.04f, true, 6));
                    if (k < segs) shapes::Append(m, BoxAA({x - 0.015f, dy1, z - 0.015f}, {x + 0.015f, y, z + 0.015f}));
                    prev = p;
                }
            }
            return m;
        }
        if (d == "truss") {
            const int n = std::max(3, (int)(L / 1.5f));
            const float ty = dy1 + 1.4f;
            for (int s = -1; s <= 1; s += 2) {
                const float z = s * (W / 2 + 0.08f);
                shapes::Append(m, BoxAA({x0, dy1 - 0.05f, z - 0.05f}, {x1, dy1 + 0.07f, z + 0.05f}));
                shapes::Append(m, BoxAA({x0, ty, z - 0.06f}, {x1, ty + 0.1f, z + 0.06f}));
                for (int k = 0; k <= n; ++k) {
                    const float x = x0 + L * (float)k / (float)n;
                    shapes::Append(m, BoxAA({x - 0.04f + 0.001f * (float)k, dy1, z - 0.035f}, {x + 0.04f + 0.001f * (float)k, ty + 0.05f, z + 0.035f}));
                    if (k < n) shapes::Append(m, Limb({x, k % 2 ? ty : dy1 + 0.05f, z}, {x0 + L * (float)(k + 1) / (float)n, k % 2 ? dy1 + 0.05f : ty, z}, 0.035f, false));
                }
            }
            return m;
        }
        if (d == "rope") {
            for (int e = 0; e < 2; ++e) for (int s = -1; s <= 1; s += 2) { const float x = e ? x1 : x0; shapes::Append(m, BoxAA({x - 0.1f, -0.3f, s * W / 2 - 0.1f}, {x + 0.1f, dy1 + 1.0f, s * W / 2 + 0.1f})); }
            return m;
        }
        // beam: piers
        for (int k = 0; k < piers; ++k) {
            const float x = piers == 1 ? 0.0f : x0 + L * 0.1f + (L * 0.8f) * (float)k / (float)(piers - 1);
            for (int s = -1; s <= 1; s += 2) shapes::Append(m, BoxAA({x - 0.15f + 0.001f * (float)k, -0.3f, s * (W / 2 - 0.2f) - 0.15f}, {x + 0.15f + 0.001f * (float)k, dy0 + 0.02f, s * (W / 2 - 0.2f) + 0.15f}));
            shapes::Append(m, BoxAA({x - 0.2f, dy0 - 0.15f, -W / 2 + 0.05f}, {x + 0.2f, dy0 + 0.04f, W / 2 - 0.05f}));
        }
        return m;
    });
    // railings
    for (int side = 0; side < 2; ++side) {
        const float z = (side ? 1.0f : -1.0f) * (W / 2 - 0.05f);
        o.Add("rail/" + std::to_string(side), "rail", FrameAA({x0, dy1, z - 0.1f}, {x1, dy1 + 1.1f, z + 0.1f}), [&](const std::string& d) {
            PolyMesh m;
            if (d == "parapet") return BoxAA({x0, dy1 - 0.02f, z - 0.15f}, {x1, dy1 + 0.8f, z + 0.15f});
            const int n = std::max(2, (int)(L / 1.5f));
            for (int k = 0; k <= n; ++k) { const float x = x0 + L * (float)k / (float)n; shapes::Append(m, BoxAA({x - 0.05f + 0.001f * (float)k, dy1 - 0.02f, z - 0.05f}, {x + 0.05f + 0.001f * (float)k, dy1 + 1.05f, z + 0.05f})); }
            if (d == "rope") {
                Vec3 prev{x0, dy1 + 0.95f, z};
                for (int k = 1; k <= n; ++k) { const float u = (float)k / (float)n; const Vec3 p{x0 + L * u, dy1 + 0.95f - 0.15f * std::sin(kPi * (u - 0.5f / (float)n)) * 0.0f, z}; shapes::Append(m, Limb(prev + Vec3{0.03f, 0, 0}, p - Vec3{0.03f, 0, 0}, 0.02f, true, 5)); prev = p; }
                return m;
            }
            shapes::Append(m, BoxAA({x0, dy1 + 0.95f, z - 0.04f}, {x1, dy1 + 1.07f, z + 0.04f}));
            shapes::Append(m, BoxAA({x0, dy1 + 0.5f, z - 0.025f}, {x1, dy1 + 0.58f, z + 0.025f}));
            return m;
        });
    }
}

Template MakeBridge() {
    B b;
    b.t.id = "bridge"; b.t.name = "Bridge"; b.t.category = "Environment"; b.t.version = 1;
    b.t.description = "A bridge along x: a deck, a structure under or over it (piers, arch, suspension cables, truss, rope posts) and railings.";
    b.P("length", "Length", 10, 2, 60, "Size", "Length of the span.");
    b.P("width", "Width", 2.2, 0.8, 8, "Size", "Width of the deck.");
    b.P("height", "Deck height", 1.5, 0.3, 8, "Size", "Height of the deck above the ground.");
    b.P("deckThickness", "Deck thickness", 0.18, 0.05, 0.8, "Size", "Thickness of the deck.");
    b.P("supports", "Piers", 3, 1, 8, "Size", "Number of piers (for the pier design).", B::K::Int);
    const Color kWood{0.5f, 0.36f, 0.22f, 1}, kStone{0.58f, 0.57f, 0.54f, 1}, kSteel{0.42f, 0.44f, 0.48f, 1}, kRope{0.7f, 0.6f, 0.4f, 1};
    b.S("deck", "Deck", {{"slab", "Slab"}, {"planks", "Planks"}, {"stone", "Stone"}, {"logs", "Logs"}}, kWood, 0);
    b.S("structure", "Structure", {{"beam", "Piers"}, {"arch", "Stone arch"}, {"suspension", "Suspension cables"}, {"truss", "Truss"}, {"rope", "Rope posts"}}, kWood, 1);
    b.S("rail", "Railing", {{"none", "None"}, {"posts", "Posts and rails"}, {"rope", "Posts and rope"}, {"parapet", "Stone parapet"}}, kWood, 2);
    b.St("footbridge", "Footbridge", "A plank deck on piers with railings.", {{"length", 8}}, {{"deck", "planks"}, {"structure", "beam"}, {"rail", "posts"}}, {});
    b.St("stone_arch", "Stone arch", "A stone bridge with an arch and parapets.", {{"length", 10}, {"height", 3}, {"width", 3.2}, {"deckThickness", 0.3}}, {{"deck", "stone"}, {"structure", "arch"}, {"rail", "parapet"}}, {{"deck", kStone}, {"structure", kStone}, {"rail", kStone}});
    b.St("rope_bridge", "Rope bridge", "Planks hung between posts with rope rails.", {{"length", 12}, {"width", 1.2}, {"deckThickness", 0.08}}, {{"deck", "planks"}, {"structure", "rope"}, {"rail", "rope"}}, {{"structure", kRope}, {"rail", kRope}});
    b.St("suspension", "Suspension bridge", "Towers and cables over a slab deck.", {{"length", 24}, {"height", 3}, {"width", 3.5}, {"deckThickness", 0.3}}, {{"deck", "slab"}, {"structure", "suspension"}, {"rail", "posts"}}, {{"deck", Color{0.4f, 0.4f, 0.42f, 1}}, {"structure", kSteel}, {"rail", kSteel}});
    b.St("truss", "Truss bridge", "A steel truss with a plank deck.", {{"length", 14}, {"height", 2}, {"width", 3}}, {{"deck", "planks"}, {"structure", "truss"}, {"rail", "none"}}, {{"structure", kSteel}});
    b.St("log", "Log bridge", "Round logs on two piers.", {{"length", 6}, {"width", 1.4}, {"supports", 2}, {"deckThickness", 0.25}}, {{"deck", "logs"}, {"structure", "beam"}, {"rail", "none"}}, {});
    b.t.generate = GenerateBridge;
    return b.t;
}

// ---- Hedge -----------------------------------------------------------------------------------------------------
void GenerateHedge(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float H = Pf(r, "height"), T = Pf(r, "thickness");
    std::vector<Vec3> pts = r.path;
    if (pts.size() < 2) pts = {{0, 0, 0}, {Pf(r, "length"), 0, 0}};
    const int segs = (int)pts.size() - 1;
    int ball = 0;
    for (int si = 0; si < segs; ++si) {
        const Vec3 a = pts[si], b2 = pts[si + 1];
        const Vec3 d{b2.x - a.x, 0, b2.z - a.z};
        const float len = d.Magnitude();
        if (len < 0.2f) continue;
        const Vec3 t = d / len, n = Vec3::Cross(t, kUp);
        const float e0 = si > 0 ? T * 0.5f - 0.004f : 0.0f, e1 = si + 1 < segs ? T * 0.5f - 0.004f : 0.0f, L = len + e0 + e1;
        const std::string sd = std::to_string(si);
        o.Add("hedge/" + sd, "hedge", FrameAA({0, 0, -T / 2}, {L, H, T / 2}), [&](const std::string& dsg) {
            const float w = T * 0.5f;
            std::vector<Vec2> prof;   // (across, up), counter-clockwise
            if (dsg == "tapered") prof = {{-w, 0}, {w, 0}, {w * 0.6f, H}, {-w * 0.6f, H}};
            else if (dsg == "rounded" || dsg == "arched") {
                prof = {{-w, 0}, {w, 0}, {w, H * 0.7f}};
                const int k = shapes::Scale(5, 2);
                const float rr = dsg == "arched" ? 1.0f : 0.6f;
                for (int i = 1; i < k; ++i) { const float a2 = kPi * (float)i / (float)k; prof.push_back({std::cos(a2) * w, H * 0.7f + std::sin(a2) * w * rr + (dsg == "arched" ? 0.3f * H : 0.0f) * std::sin(a2)}); }
                prof.push_back({-w, H * 0.7f});
            } else prof = {{-w, 0}, {w, 0}, {w, H}, {-w, H}};
            // profile across (z) and up, pushed along x: a = z axis, b = up, normal = x
            PolyMesh m = shapes::ExtrudeAxes({L * 0.5f, 0, 0}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0}, prof, L);
            PlaceSheared(m, a - t * e0, t, n, 0.0f);
            return m;
        });
        // top decoration
        const std::string top = r.DesignFor("topiary", "topiary/" + sd);
        if (top != "none" && !top.empty()) {
            const int cnt = std::max(1, (int)(len / 1.6f));
            for (int k = 0; k < cnt; ++k) {
                const float x = len * ((float)k + 0.5f) / (float)cnt;
                const Vec3 p = a + t * x + kUp * H;
                const int id = ball++;
                o.Add("topiary/" + std::to_string(id), "topiary", FrameAA({p.x - 0.3f, p.y - 0.1f, p.z - 0.3f}, {p.x + 0.3f, p.y + 0.6f, p.z + 0.3f}), [&](const std::string& d) {
                    PolyMesh m;
                    if (d == "cone") return Cone({p.x, p.y - 0.05f, p.z}, {p.x, p.y + 0.8f, p.z}, T * 0.45f, 8);
                    if (d == "spiral") { for (int q = 0; q < 3; ++q) shapes::Append(m, Ell({p.x, p.y + 0.2f + 0.3f * (float)q, p.z}, T * (0.5f - 0.12f * (float)q), 0.17f, T * (0.5f - 0.12f * (float)q), 4, 8)); return m; }
                    return Ell({p.x, p.y + 0.15f, p.z}, T * 0.5f, T * 0.5f, T * 0.5f, 5, 8);
                });
            }
        }
    }
}

Template MakeHedge() {
    B b;
    b.t.id = "hedge"; b.t.name = "Hedge"; b.t.category = "Environment"; b.t.version = 1; b.t.usesPath = true;
    b.t.description = "A hedge along a straight run or a path you draw: a clipped shape, with optional topiary on top.";
    b.P("length", "Length", 6, 0.5, 100, "Size", "Length of a straight hedge (ignored when you draw a path).");
    b.P("height", "Height", 1.4, 0.3, 4, "Size", "Hedge height.");
    b.P("thickness", "Thickness", 0.8, 0.2, 2, "Size", "Hedge thickness.");
    const Color kHedge{0.18f, 0.42f, 0.2f, 1};
    b.S("hedge", "Shape", {{"box", "Square"}, {"rounded", "Rounded top"}, {"tapered", "Tapered"}, {"arched", "Domed"}}, kHedge, 0);
    b.S("topiary", "Topiary", {{"none", "None"}, {"ball", "Balls"}, {"cone", "Cones"}, {"spiral", "Stacked balls"}}, kHedge, 1);
    b.St("box", "Clipped box hedge", "Square and flat on top.", {}, {{"hedge", "box"}}, {});
    b.St("rounded", "Rounded hedge", "A soft rounded top.", {{"height", 1.6}}, {{"hedge", "rounded"}}, {});
    b.St("tall_screen", "Tall screen", "A thin tapered privacy hedge.", {{"height", 2.6}, {"thickness", 0.6}}, {{"hedge", "tapered"}}, {});
    b.St("low_border", "Low border", "A knee-high edging.", {{"height", 0.5}, {"thickness", 0.4}}, {{"hedge", "rounded"}}, {{"hedge", Color{0.3f, 0.5f, 0.25f, 1}}});
    b.St("domed", "Domed hedge", "A fat domed shape.", {{"height", 1.8}, {"thickness", 1.1}}, {{"hedge", "arched"}}, {});
    b.St("topiary_balls", "Hedge with balls", "A clipped hedge with a ball on top every 1.6 m.", {}, {{"hedge", "box"}, {"topiary", "ball"}}, {});
    b.St("topiary_cones", "Hedge with cones", "Cones on top.", {{"height", 1.2}}, {{"hedge", "rounded"}, {"topiary", "cone"}}, {});
    b.t.generate = GenerateHedge;
    return b.t;
}

// ---- Plant -----------------------------------------------------------------------------------------------------
void GeneratePlant(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float sz = Pf(r, "size"), spread = Pf(r, "spread");
    const int count = std::max(1, (int)r.Param("count"));
    Rnd rnd((uint32_t)r.Param("seed"));
    for (int i = 0; i < count; ++i) {
        const float a = rnd.R(0, 2 * kPi), d = i == 0 ? 0.0f : spread * rnd.R(0.3f, 1.0f);
        const float s = sz * (i == 0 ? 1.0f : rnd.R(0.6f, 1.0f));
        const Vec3 c{std::cos(a) * d + 0.0017f * (float)i, 0, std::sin(a) * d};
        const std::string sd = std::to_string(i);
        o.Add("plant/" + sd, "plant", FrameAA({c.x - s * 0.5f, 0, c.z - s * 0.5f}, {c.x + s * 0.5f, s, c.z + s * 0.5f}), [&](const std::string& dsg) {
            PolyMesh m;
            if (dsg == "flower" || dsg == "sunflower") {
                const float hh = dsg == "sunflower" ? s * 1.6f : s * 0.7f, pr = dsg == "sunflower" ? s * 0.22f : s * 0.1f;
                shapes::Append(m, Limb({c.x, 0, c.z}, {c.x, hh, c.z}, 0.015f * s + 0.008f, true, 6));
                const int petals = dsg == "sunflower" ? 10 : 5;
                for (int k = 0; k < petals; ++k) { const float pa = 2 * kPi * (float)k / (float)petals; shapes::Append(m, Ell({c.x + std::cos(pa) * pr * 1.1f, hh + 0.01f, c.z + std::sin(pa) * pr * 1.1f}, pr * 0.55f, pr * 0.12f, pr * 0.35f, 3, 5)); }
                shapes::Append(m, Ell({c.x, hh + 0.03f, c.z}, pr * 0.5f, pr * 0.25f, pr * 0.5f, 4, 6));
                // leaves
                for (int k = 0; k < 2; ++k) shapes::Append(m, Cone({c.x, hh * 0.3f, c.z}, {c.x + (k ? 1 : -1) * s * 0.3f, hh * 0.45f, c.z}, s * 0.06f, 4));
                return m;
            }
            if (dsg == "bush") { for (int k = 0; k < 4; ++k) { const float rr = s * rnd.R(0.3f, 0.45f); shapes::Append(m, Ell({c.x + rnd.R(-0.25f, 0.25f) * s, rr * 0.8f, c.z + rnd.R(-0.25f, 0.25f) * s}, rr, rr * 0.8f, rr, 4, 8)); } return m; }
            if (dsg == "fern") { const int n = 9; for (int k = 0; k < n; ++k) { const float fa = 2 * kPi * (float)k / (float)n; shapes::Append(m, Cone({c.x, 0.02f, c.z}, {c.x + std::cos(fa) * s * 0.7f, s * 0.35f, c.z + std::sin(fa) * s * 0.7f}, s * 0.07f, 4)); } return m; }
            if (dsg == "cactus") { shapes::Append(m, Limb({c.x, 0, c.z}, {c.x, s * 1.1f, c.z}, s * 0.14f, true, 8)); for (int k = -1; k <= 1; k += 2) { shapes::Append(m, Limb({c.x + k * s * 0.12f, s * 0.5f, c.z}, {c.x + k * s * 0.38f, s * 0.5f, c.z}, s * 0.07f, true, 6)); shapes::Append(m, Limb({c.x + k * s * 0.38f, s * 0.48f, c.z}, {c.x + k * s * 0.38f, s * (0.8f + 0.1f * (float)k), c.z}, s * 0.065f, true, 6)); } return m; }
            if (dsg == "mushroom") { shapes::Append(m, Limb({c.x, 0, c.z}, {c.x, s * 0.4f, c.z}, s * 0.07f, true, 8)); shapes::Append(m, Ell({c.x, s * 0.43f, c.z}, s * 0.25f, s * 0.14f, s * 0.25f, 4, 10)); return m; }
            if (dsg == "reeds") { for (int k = 0; k < 9; ++k) { const float ra = rnd.R(0, 2 * kPi), rd = rnd.R(0, s * 0.3f); const Vec3 b0{c.x + std::cos(ra) * rd, 0, c.z + std::sin(ra) * rd}; shapes::Append(m, Limb(b0, b0 + Vec3{rnd.R(-0.1f, 0.1f), s * rnd.R(0.9f, 1.5f), rnd.R(-0.1f, 0.1f)}, 0.012f, false)); } return m; }
            // grass tuft
            for (int k = 0; k < 8; ++k) { const float ga = 2 * kPi * (float)k / 8.0f + rnd.R(0, 0.3f); shapes::Append(m, Cone({c.x + std::cos(ga) * 0.04f, 0.0f, c.z + std::sin(ga) * 0.04f}, {c.x + std::cos(ga) * s * 0.25f, s * rnd.R(0.4f, 0.7f), c.z + std::sin(ga) * s * 0.25f}, 0.025f * s + 0.01f, 3)); }
            return m;
        });
        o.Add("pot/" + sd, "pot", FrameAA({c.x - s * 0.3f, -0.25f * s, c.z - s * 0.3f}, {c.x + s * 0.3f, 0.02f, c.z + s * 0.3f}), [&](const std::string& d) {
            Frame f = FrameAA({c.x - s * 0.3f, -0.25f * s, c.z - s * 0.3f}, {c.x + s * 0.3f, 0.02f, c.z + s * 0.3f});
            if (d == "box") return BoxAA({c.x - s * 0.4f, -0.25f * s, c.z - s * 0.3f}, {c.x + s * 0.4f, 0.02f, c.z + s * 0.3f});
            return shapes::Prism(f, 12);
        });
    }
}

Template MakePlant() {
    B b;
    b.t.id = "plant"; b.t.name = "Plant"; b.t.category = "Environment"; b.t.version = 1;
    b.t.description = "Plants: bushes, flowers, ferns, cacti, mushrooms, reeds and grass, one or a patch, optionally in a pot.";
    b.P("size", "Size", 1.0, 0.1, 4, "Size", "Size of the biggest plant.");
    b.P("count", "How many", 1, 1, 20, "Size", "Number of plants in the patch.", B::K::Int);
    b.P("spread", "Patch radius", 1.0, 0.1, 6, "Size", "How far apart the plants stand.");
    b.P("seed", "Variation (seed)", 1, 0, 999, "Size", "Another number makes a different patch.", B::K::Int);
    const Color kGreen{0.25f, 0.55f, 0.25f, 1}, kClay{0.7f, 0.4f, 0.28f, 1};
    b.S("plant", "Plant", {{"bush", "Bush"}, {"flower", "Flower"}, {"sunflower", "Sunflower"}, {"fern", "Fern"}, {"cactus", "Cactus"}, {"mushroom", "Mushroom"}, {"reeds", "Reeds"}, {"grass", "Grass tuft"}}, kGreen, 0);
    b.S("pot", "Pot", {{"none", "None"}, {"clay", "Clay pot"}, {"box", "Planter box"}}, kClay, 1);
    b.St("bush", "Bush", "A round leafy bush.", {{"size", 1.2}}, {{"plant", "bush"}}, {});
    b.St("flowers", "Flower patch", "Small flowers scattered about.", {{"count", 9}, {"size", 0.5}, {"spread", 0.9}}, {{"plant", "flower"}}, {{"plant", Color{0.85f, 0.3f, 0.45f, 1}}});
    b.St("sunflowers", "Sunflowers", "Tall yellow flowers.", {{"count", 5}, {"size", 0.8}, {"spread", 1.2}, {"seed", 3}}, {{"plant", "sunflower"}}, {{"plant", Color{0.9f, 0.75f, 0.1f, 1}}});
    b.St("ferns", "Ferns", "A patch of fern fronds.", {{"count", 4}, {"size", 0.9}}, {{"plant", "fern"}}, {{"plant", Color{0.2f, 0.5f, 0.2f, 1}}});
    b.St("cactus", "Cactus", "A branching cactus.", {{"size", 1.4}}, {{"plant", "cactus"}}, {{"plant", Color{0.3f, 0.55f, 0.35f, 1}}});
    b.St("mushrooms", "Mushrooms", "Red-capped toadstools.", {{"count", 5}, {"size", 0.4}, {"spread", 0.5}}, {{"plant", "mushroom"}}, {{"plant", Color{0.8f, 0.2f, 0.15f, 1}}});
    b.St("reeds", "Reeds", "Tall thin stalks.", {{"count", 4}, {"size", 1.4}}, {{"plant", "reeds"}}, {{"plant", Color{0.55f, 0.6f, 0.3f, 1}}});
    b.St("grass", "Grass", "A few grass tufts.", {{"count", 6}, {"size", 0.5}, {"spread", 0.8}}, {{"plant", "grass"}}, {{"plant", Color{0.35f, 0.6f, 0.25f, 1}}});
    b.St("potted", "Potted plant", "A bush in a clay pot.", {{"size", 0.7}}, {{"plant", "bush"}, {"pot", "clay"}}, {});
    b.St("planter", "Planter box", "Flowers in a long box.", {{"count", 4}, {"size", 0.4}, {"spread", 0.4}}, {{"plant", "flower"}, {"pot", "box"}}, {{"plant", Color{0.9f, 0.5f, 0.2f, 1}}});
    b.t.generate = GeneratePlant;
    return b.t;
}

// ---- Street lamp -------------------------------------------------------------------------------------------------
void GenerateLamp(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float H = Pf(r, "height"), pr = Pf(r, "poleRadius"), hs = Pf(r, "headSize"), arm = Pf(r, "armLength");
    o.Add("base/0", "base", FrameAA({-pr * 3, 0, -pr * 3}, {pr * 3, 0.4f, pr * 3}), [&](const std::string& d) {
        if (d == "flared") { Frame f = FrameAA({-pr * 3, 0, -pr * 3}, {pr * 3, 0.5f, pr * 3}); PolyMesh m = shapes::Prism(f, 8); Frame g = FrameAA({-pr * 2, 0.45f, -pr * 2}, {pr * 2, 0.8f, pr * 2}); shapes::Append(m, shapes::Prism(g, 8)); return m; }
        return BoxAA({-pr * 3, 0, -pr * 3}, {pr * 3, 0.35f, pr * 3});
    });
    o.Add("pole/0", "pole", FrameAA({-pr, 0.3f, -pr}, {pr, H, pr}), [&](const std::string& d) {
        Frame f = FrameAA({-pr, 0.3f, -pr}, {pr, H, pr});
        if (d == "square") return shapes::Box(f);
        if (d == "tapered") { PolyMesh m = shapes::Prism(f, 8); f.sx = f.sz = pr * 1.4f; f.origin.y = H * 0.55f; f.sy = H * 0.45f; (void)m; PolyMesh a = shapes::Prism(FrameAA({-pr * 1.4f, 0.3f, -pr * 1.4f}, {pr * 1.4f, H * 0.55f, pr * 1.4f}), 8); shapes::Append(a, shapes::Prism(FrameAA({-pr * 0.8f, H * 0.5f, -pr * 0.8f}, {pr * 0.8f, H, pr * 0.8f}), 8)); return a; }
        if (d == "ornate") { PolyMesh m = shapes::Prism(f, 10); for (int k = 1; k <= 3; ++k) shapes::Append(m, shapes::Prism(FrameAA({-pr * 1.6f, H * 0.2f * (float)k, -pr * 1.6f}, {pr * 1.6f, H * 0.2f * (float)k + 0.08f, pr * 1.6f}), 10)); return m; }
        return shapes::Prism(f, 10);
    });
    const std::string armD = r.DesignFor("arm", "arm/0");
    float hx = 0;
    if (armD != "none" && armD != "") {
        o.Add("arm/0", "arm", FrameAA({-arm, H - 0.3f, -pr}, {arm, H + 0.3f, pr}), [&](const std::string& d) {
            PolyMesh m;
            const int sides = d == "double" ? 2 : 1;
            for (int s = 0; s < sides; ++s) {
                const float sg = d == "double" ? (s ? 1.0f : -1.0f) : 1.0f;
                shapes::Append(m, Limb({sg * 0.02f, H - 0.15f, 0}, {sg * arm * 0.5f, H + 0.2f, 0}, pr * 0.55f, true, 6));
                shapes::Append(m, Limb({sg * arm * 0.5f, H + 0.18f, 0}, {sg * arm, H + 0.12f, 0}, pr * 0.5f, true, 6));
            }
            return m;
        });
        hx = arm;
    }
    auto headAt = [&](float x, const std::string& id) {
        const float y = H + (hx > 0 ? 0.12f : 0.05f);
        o.Add(id, "head", FrameAA({x - hs, y, -hs}, {x + hs, y + hs * 1.8f, hs}), [&](const std::string& d) {
            PolyMesh m;
            if (d == "lantern") { m = BoxAA({x - hs * 0.45f, y + hs * 0.2f, -hs * 0.45f}, {x + hs * 0.45f, y + hs * 1.2f, hs * 0.45f}); shapes::Append(m, Cone({x, y + hs * 1.15f, 0}, {x, y + hs * 1.8f, 0}, hs * 0.7f, 4)); shapes::Append(m, BoxAA({x - hs * 0.55f, y, -hs * 0.55f}, {x + hs * 0.55f, y + hs * 0.22f, hs * 0.55f})); return m; }
            if (d == "box") return BoxAA({x - hs * 1.1f, y + hs * 0.1f, -hs * 0.5f}, {x + hs * 1.1f, y + hs * 0.5f, hs * 0.5f});
            if (d == "bowl") { m = Ell({x, y + hs * 0.6f, 0}, hs * 0.8f, hs * 0.45f, hs * 0.8f, 5, 10); shapes::Append(m, Cone({x, y + hs * 0.9f, 0}, {x, y + hs * 1.5f, 0}, hs * 0.9f, 10)); return m; }
            return Ell({x, y + hs * 0.8f, 0}, hs * 0.75f, hs * 0.8f, hs * 0.75f, 6, 10);   // globe
        });
    };
    if (armD == "double") { headAt(-arm, "head/0"); headAt(arm, "head/1"); }
    else if (hx > 0) headAt(arm, "head/0");
    else headAt(0, "head/0");
}

Template MakeLamp() {
    B b;
    b.t.id = "streetlamp"; b.t.name = "Street lamp"; b.t.category = "Environment"; b.t.version = 1;
    b.t.description = "A lamp post: base, pole, an optional arm and one or two lamp heads.";
    b.P("height", "Height", 4.5, 0.4, 12, "Size", "Height of the pole.");
    b.P("poleRadius", "Pole radius", 0.07, 0.02, 0.3, "Size", "Radius of the pole.");
    b.P("headSize", "Lamp size", 0.25, 0.08, 0.8, "Size", "Size of the lamp.");
    b.P("armLength", "Arm length", 0.8, 0.2, 3, "Size", "How far the arm reaches (when it has one).");
    const Color kIron{0.12f, 0.14f, 0.12f, 1}, kLight{0.98f, 0.92f, 0.65f, 1}, kGrey{0.5f, 0.52f, 0.55f, 1};
    b.S("base", "Base", {{"plinth", "Plinth"}, {"flared", "Flared"}}, kIron, 0);
    b.S("pole", "Pole", {{"round", "Round"}, {"square", "Square"}, {"tapered", "Tapered"}, {"ornate", "Ornate"}}, kIron, 0);
    b.S("arm", "Arm", {{"none", "None"}, {"curved", "Curved arm"}, {"double", "Two arms"}}, kIron, 1);
    b.S("head", "Lamp", {{"globe", "Globe"}, {"lantern", "Lantern"}, {"box", "Modern box"}, {"bowl", "Shade"}}, kLight, 2);
    b.St("victorian", "Victorian", "An ornate post with a lantern.", {{"height", 4}}, {{"base", "flared"}, {"pole", "ornate"}, {"head", "lantern"}}, {});
    b.St("modern", "Modern", "A tall square pole with a box lamp on an arm.", {{"height", 7}, {"poleRadius", 0.09}, {"armLength", 1.6}}, {{"pole", "tapered"}, {"arm", "curved"}, {"head", "box"}}, {{"pole", kGrey}, {"arm", kGrey}, {"base", kGrey}});
    b.St("bollard", "Garden bollard", "A short post with a globe.", {{"height", 0.9}, {"poleRadius", 0.06}, {"headSize", 0.14}}, {{"pole", "round"}, {"head", "globe"}}, {});
    b.St("double_arm", "Double lamp", "Two lamps on arms.", {{"height", 5.5}, {"armLength", 1.1}}, {{"pole", "round"}, {"arm", "double"}, {"head", "bowl"}}, {});
    b.St("lantern_post", "Lantern post", "A square post with a lantern.", {{"height", 2.8}}, {{"pole", "square"}, {"head", "lantern"}}, {{"pole", Color{0.4f, 0.28f, 0.18f, 1}}});
    b.St("rural", "Rural lamp", "A plain pole with a shade on an arm.", {{"height", 5}}, {{"pole", "round"}, {"arm", "curved"}, {"head", "bowl"}}, {{"pole", Color{0.45f, 0.32f, 0.2f, 1}}});
    b.t.generate = GenerateLamp;
    return b.t;
}

} // namespace

void RegisterEnvironmentTemplates() {
    Registry& r = Registry::Instance();
    r.Register(MakeTree());
    r.Register(MakeRock());
    r.Register(MakeRoad());
    r.Register(MakeBridge());
    r.Register(MakeHedge());
    r.Register(MakePlant());
    r.Register(MakeLamp());
}

} // namespace okay::modeling::guided
