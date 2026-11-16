// Headless pictures for the guided modeling docs (docs/images/guided_modeling/). A tiny software rasteriser of our own
// (z-buffer, flat shading, face colours) and a minimal PNG writer, so no window or GPU is needed. These are
// renders of the generated meshes, not screenshots of the editor.
//   guided_pictures <picture folder> [<example folder>]
// With the second folder it also writes the shipped example (examples/modeling): one .okaymesh and .okayguided per fence
// style and a scene that uses them, all made by this code.
#include "okay/Modeling/Guided.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::modeling;
using namespace okay::modeling::guided;

namespace {

struct Img {
    int w, h;
    std::vector<uint8_t> px;     // RGB
    std::vector<float> z;
    Img(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 3), z((size_t)w_ * h_, 1e30f) {}
    void Clear(uint8_t r, uint8_t g, uint8_t b) { for (size_t i = 0; i < px.size(); i += 3) { px[i] = r; px[i + 1] = g; px[i + 2] = b; } std::fill(z.begin(), z.end(), 1e30f); }
};

uint32_t Crc(const uint8_t* d, size_t n, uint32_t c = 0xffffffffu) {
    static uint32_t t[256];
    static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; ++i) { uint32_t x = i; for (int k = 0; k < 8; ++k) x = x & 1 ? 0xedb88320u ^ (x >> 1) : x >> 1; t[i] = x; } init = true; }
    for (size_t i = 0; i < n; ++i) c = t[(c ^ d[i]) & 255] ^ (c >> 8);
    return c;
}
void Put32(std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s)); }
void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    Put32(out, (uint32_t)data.size());
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    Put32(out, Crc(body.data(), body.size()) ^ 0xffffffffu);
}
bool WritePng(const std::string& path, const Img& im) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < im.h; ++y) { raw.push_back(0); raw.insert(raw.end(), im.px.begin() + (size_t)y * im.w * 3, im.px.begin() + (size_t)(y + 1) * im.w * 3); }
    std::vector<uint8_t> z = {0x78, 0x01};   // zlib header, stored blocks
    for (size_t p = 0; p < raw.size();) {
        const size_t n = std::min<size_t>(65535, raw.size() - p);
        z.push_back(p + n >= raw.size() ? 1 : 0);
        z.push_back(n & 255); z.push_back(n >> 8); z.push_back(~n & 255); z.push_back((~n >> 8) & 255);
        z.insert(z.end(), raw.begin() + p, raw.begin() + p + n);
        p += n;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    Put32(z, (b << 16) | a);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 13, 10, 26, 10}, ihdr;
    Put32(ihdr, im.w); Put32(ihdr, im.h);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    Chunk(out, "IHDR", ihdr); Chunk(out, "IDAT", z); Chunk(out, "IEND", {});
    std::ofstream f(path, std::ios::binary);
    f.write((const char*)out.data(), (std::streamsize)out.size());
    return f.good();
}

struct Cam { Vec3 eye, target; float fov = 0.9f; };

void Raster(Img& im, const PolyMesh& m, const Cam& cam, int x0, int y0, int w, int h) {
    const Vec3 fwd = (cam.target - cam.eye).Normalized();
    const Vec3 right = Vec3::Cross(fwd, Vec3{0, 1, 0}).Normalized();
    const Vec3 up = Vec3::Cross(right, fwd);
    const float f = 0.5f * (float)h / std::tan(cam.fov * 0.5f);
    auto proj = [&](const Vec3& p, float& sx, float& sy, float& d) {
        const Vec3 q = p - cam.eye;
        d = Vec3::Dot(q, fwd);
        sx = x0 + w * 0.5f + f * Vec3::Dot(q, right) / d;
        sy = y0 + h * 0.5f - f * Vec3::Dot(q, up) / d;
    };
    const Vec3 light = Vec3{0.4f, 0.8f, 0.5f}.Normalized();
    for (int fi = 0; fi < (int)m.faces.size(); ++fi) {
        const PolyFace& face = m.faces[fi];
        Vec3 n = m.FaceNormal(fi);
        if (Vec3::Dot(n, cam.eye - m.verts[face.verts[0]]) <= 0) continue;   // back face
        const float lit = 0.35f + 0.65f * std::max(0.0f, Vec3::Dot(n.Normalized(), light));
        const Color c = face.hasColor ? face.color : Color{0.8f, 0.8f, 0.8f, 1};
        const std::vector<int> tri = m.TriangulateFace(fi);
        for (size_t k = 0; k + 2 < tri.size(); k += 3) {
            float sx[3], sy[3], d[3];
            for (int i = 0; i < 3; ++i) proj(m.verts[face.verts[tri[k + i]]], sx[i], sy[i], d[i]);
            if (d[0] < 0.1f || d[1] < 0.1f || d[2] < 0.1f) continue;
            const int minx = std::max(x0, (int)std::floor(std::min({sx[0], sx[1], sx[2]})));
            const int maxx = std::min(x0 + w - 1, (int)std::ceil(std::max({sx[0], sx[1], sx[2]})));
            const int miny = std::max(y0, (int)std::floor(std::min({sy[0], sy[1], sy[2]})));
            const int maxy = std::min(y0 + h - 1, (int)std::ceil(std::max({sy[0], sy[1], sy[2]})));
            const float den = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2]);
            if (std::fabs(den) < 1e-9f) continue;
            for (int y = miny; y <= maxy; ++y)
                for (int x = minx; x <= maxx; ++x) {
                    const float px = x + 0.5f, py = y + 0.5f;
                    const float l0 = ((sy[1] - sy[2]) * (px - sx[2]) + (sx[2] - sx[1]) * (py - sy[2])) / den;
                    const float l1 = ((sy[2] - sy[0]) * (px - sx[2]) + (sx[0] - sx[2]) * (py - sy[2])) / den;
                    const float l2 = 1 - l0 - l1;
                    if (l0 < -1e-4f || l1 < -1e-4f || l2 < -1e-4f) continue;
                    const float z = l0 * d[0] + l1 * d[1] + l2 * d[2];
                    const size_t idx = (size_t)y * im.w + x;
                    if (z >= im.z[idx]) continue;
                    im.z[idx] = z;
                    im.px[idx * 3] = (uint8_t)std::min(255.0f, c.r * lit * 255);
                    im.px[idx * 3 + 1] = (uint8_t)std::min(255.0f, c.g * lit * 255);
                    im.px[idx * 3 + 2] = (uint8_t)std::min(255.0f, c.b * lit * 255);
                }
        }
    }
}

// A sky-to-grass backdrop, then the model, framed by its bounds.
void Scene(Img& im, const PolyMesh& m, int x0, int y0, int w, int h, float yaw = 0.55f) {
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x) {
            const float t = (float)(y - y0) / (float)h;
            uint8_t* p = &im.px[((size_t)y * im.w + x) * 3];
            if (t < 0.62f) { p[0] = (uint8_t)(170 + 40 * t); p[1] = (uint8_t)(200 + 30 * t); p[2] = 235; }
            else { p[0] = 96; p[1] = (uint8_t)(140 - 20 * t); p[2] = 80; }
            im.z[(size_t)y * im.w + x] = 1e30f;
        }
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    for (const Vec3& v : m.verts) { lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)}; hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)}; }
    const Vec3 c = (lo + hi) * 0.5f;
    const float r = std::max(0.5f, (hi - lo).Magnitude() * 0.5f);
    Cam cam;
    cam.target = c;
    cam.eye = c + Vec3{std::sin(yaw) * std::cos(0.35f), std::sin(0.35f), std::cos(yaw) * std::cos(0.35f)} * (r * 2.1f);
    Raster(im, m, cam, x0, y0, w, h);
}

void Save(const std::string& dir, const std::string& name, const Img& im) {
    const std::string p = dir + "/" + name;
    std::printf("%s %s\n", WritePng(p, im) ? "wrote" : "FAILED", p.c_str());
}

} // namespace

// A Main Camera for an example scene: in front of the models (+Z side, the engine's camera looks down -Z), tilted down a little.
static std::string CameraRecord(int id, double x, double y, double z) {
    char b[400];
    std::snprintf(b, sizeof b, "gameobject %d \"Main Camera\"\n  active 1\n  parent -1\n  transform %g %g %g -0.13 0 0 0.9915 1 1 1\n"
                  "  camera 1 5 50 0.55 0.72 0.88 1 1 0 0 0.3 4000 0 0 0 1 1 -1 0 50 24 1 0 0 0 0 32 0.4 3 14 1.5\nend\n", id, x, y, z);
    return b;
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "docs/images/guided_modeling";
    std::filesystem::create_directories(dir);
    RegisterBuiltinTemplates();
    const Template& T = *Registry::Instance().Find("fence");

    // 1. The design browser's thumbnails (what the Guided page shows), and one picture per style.
    {
        const int cols = 4, tw = 360, th = 230;
        const int rows = ((int)T.starts.size() + cols - 1) / cols;
        Img sheet(cols * tw, rows * th);
        sheet.Clear(40, 42, 48);
        for (int i = 0; i < (int)T.starts.size(); ++i) {
            Recipe r = DefaultRecipe(T, T.starts[i].id);
            r.params["length"] = 5;
            const PolyMesh m = Create(r);
            Scene(sheet, m, (i % cols) * tw + 4, (i / cols) * th + 4, tw - 8, th - 8);
            Img one(640, 360);
            Scene(one, m, 0, 0, 640, 360);
            Save(dir, "style_" + T.starts[i].id + ".png", one);
        }
        Save(dir, "design_browser_thumbnails.png", sheet);
    }

    // 2. A customised fence: picket start, taller, scalloped, a gate, a sloping ground, new colours.
    Recipe a = DefaultRecipe(T, "picket");
    a.params["length"] = 7; a.params["height"] = 1.35; a.params["infillAlt"] = 0.06; a.params["gate"] = 1; a.params["gatePos"] = 0.5;
    a.params["gateWidth"] = 1.2; a.params["slope"] = 8; a.params["postSpacing"] = 2.4;
    a.slotDesign["infill"] = "rounded"; a.slotDesign["cap"] = "ball";
    for (const char* s : {"post", "cap", "rail", "infill", "gate"}) a.colors[s] = Color{0.30f, 0.50f, 0.78f, 1};
    a.colors["gate"] = Color{0.85f, 0.35f, 0.25f, 1};
    {
        Img im(800, 450);
        Scene(im, Create(a), 0, 0, 800, 450);
        Save(dir, "customised_fence.png", im);
    }

    // 3. Part swap: the same fence, only the posts and one picket replaced.
    {
        const Recipe base = DefaultRecipe(T, "picket");
        Recipe swapped = base;
        swapped.slotDesign["post"] = "round";
        swapped.slotDesign["cap"] = "ball";
        swapped.partDesign["infill/0/2"] = "spear";
        swapped.partDesign["infill/0/3"] = "dogear";
        Img im(1200, 360);
        im.Clear(0, 0, 0);
        Scene(im, Create(base), 0, 0, 600, 360);
        Scene(im, Regenerate(Create(base), base, swapped).mesh, 600, 0, 600, 360);
        Save(dir, "part_swap_before_after.png", im);
    }

    // 4. A manual edit surviving a regeneration: a custom box, a pulled post and a lifted picket; then the height changes.
    {
        const Recipe base = DefaultRecipe(T, "picket");
        PolyMesh m = Create(base);
        const Analysis an = Analyze(m, base);
        std::set<int> lift;
        for (int i = 0; i < (int)an.faceOwner.size(); ++i) { if (an.faceOwner[i] == "infill/0/4") lift.insert(i); }
        std::set<int> vs;
        for (int f : lift) for (int v : m.faces[f].verts) vs.insert(v);
        for (int v : vs) m.verts[v].y += 0.18f;
        // a custom crate beside the fence (the user's own geometry)
        PolyMesh crate;
        const float s = 0.3f;
        const Vec3 c0{1.2f, s, 0.8f};
        const Vec3 cs[8] = {{-s, -s, -s}, {s, -s, -s}, {s, s, -s}, {-s, s, -s}, {-s, -s, s}, {s, -s, s}, {s, s, s}, {-s, s, s}};
        for (const Vec3& p : cs) crate.verts.push_back(p + c0);
        const int q[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
        for (auto& f : q) { PolyFace pf; pf.verts = {f[0], f[1], f[2], f[3]}; pf.hasColor = true; pf.color = Color{0.9f, 0.6f, 0.2f, 1}; crate.faces.push_back(pf); }
        shapes::Append(m, crate);
        Recipe next = base;
        next.params["height"] = 1.5;
        next.params["length"] = 7;
        RegenOptions keep; keep.all = Decision::Keep;   // the lifted picket is an edit: the user chose "keep my edits"
        const RegenResult r = Regenerate(m, base, next, keep);
        std::printf("regen: ok=%d needsDecision=%d %s\n", (int)r.ok, (int)r.needsDecision, r.message.c_str());
        Img im(1200, 360);
        im.Clear(0, 0, 0);
        Scene(im, m, 0, 0, 600, 360);
        Scene(im, r.mesh, 600, 0, 600, 360);
        Save(dir, "manual_edit_survives_regeneration.png", im);
    }
    if (argc > 2) {
        const std::string ex = argv[2];
        std::filesystem::create_directories(ex);
        std::string scene =
            "okayscene 3\nname \"Guided fences\"\ngravity 0 -9.81\ngravity3 0 -9.81 0\n"
            "rendersettings 1 0.27450982 0.47058824 0.78431374 0.5882353 0.7254902 0.88235295 0.47058824 0.47058824 0.50980395 0.15\n"
            "fog 0 0.5882353 0.7254902 0.88235295 20 90\ntonemap 1\n"
            "gameobject 0 \"Directional Light\"\n  active 1\n  parent -1\n  transform 0 0 0 0.40821794 -0.23456973 0.10938166 0.8754261 1 1 1\n"
            "  light 1 1 1 1 0.3 1 0 10 45 0.5 0 6500 1 1 1 0\nend\n"
            "gameobject 1 \"Ground\"\n  active 1\n  parent -1\n  transform 0 -0.5 0 0 0 0 1 60 1 60\n"
            "  mesh \"Cube\" 0.32941177 0.5019608 0.27450982 1 0 \"\" 0\n  material 0 0 0 0 16 0 \"\" 1 1\nend\n";
        int id = 2;
        std::vector<std::pair<std::string, Recipe>> items;
        for (const StartDesign& st : T.starts) items.push_back({st.id, DefaultRecipe(T, st.id)});
        Recipe gated = DefaultRecipe(T, "picket");
        gated.params["gate"] = 1;
        items.push_back({"picket_with_gate", gated});
        for (size_t i = 0; i < items.size(); ++i) {
            const std::string base = "fence_" + items[i].first;
            const PolyMesh m = Create(items[i].second);
            m.Save(ex + "/" + base + ".okaymesh");
            SaveRecipe(SidecarPath(ex + "/" + base + ".okaymesh"), items[i].second);
            char tr[160];
            std::snprintf(tr, sizeof tr, "  transform -3 0 %g 0 0 0 1 1 1 1\n", (double)i * 2.5 - 8.0);
            scene += "gameobject " + std::to_string(id++) + " \"Fence " + items[i].first + "\"\n  active 1\n  parent -1\n" + tr +
                     "  mesh \"\" 1 1 1 1 0 \"" + base + ".okaymesh\" 0\n  material 0 0 0 0 16 0 \"\" 1 1\nend\n";
        }
        scene += CameraRecord(id++, -3.0, 6.0, 26.0);
        std::ofstream("" + ex + "/guided_fences.okayscene", std::ios::binary) << scene;
        std::printf("wrote the example folder %s (%d fences)\n", ex.c_str(), (int)items.size());
    }
    return 0;
}
