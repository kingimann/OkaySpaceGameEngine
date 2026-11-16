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

static bool g_edges = false;   // draw triangle edges (the detail pictures)
static bool g_smooth = false;  // smooth shading: corner normals averaged over the faces within g_smoothAngle (like the editor's auto-smooth)
static float g_elev = 0.35f;   // camera elevation (radians) used by Scene()
static float g_zoom = 2.1f;    // camera distance in model radii used by Scene()
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
    std::vector<Vec3> fnorm(m.faces.size());
    std::vector<std::vector<int>> vfaces;
    if (g_smooth) {
        vfaces.assign(m.verts.size(), {});
        for (int fi = 0; fi < (int)m.faces.size(); ++fi) { fnorm[fi] = m.FaceNormal(fi).Normalized(); for (int v : m.faces[fi].verts) vfaces[v].push_back(fi); }
    }
    auto cornerNormal = [&](int fi, int v) {
        Vec3 n = fnorm[fi];
        for (int g : vfaces[v]) if (g != fi && Vec3::Dot(fnorm[g], fnorm[fi]) > 0.70f) n = n + fnorm[g];
        return n.Normalized();
    };
    for (int fi = 0; fi < (int)m.faces.size(); ++fi) {
        const PolyFace& face = m.faces[fi];
        Vec3 n = m.FaceNormal(fi);
        if (Vec3::Dot(n, cam.eye - m.verts[face.verts[0]]) <= 0) continue;   // back face
        const float lit = 0.35f + 0.65f * std::max(0.0f, Vec3::Dot(n.Normalized(), light));
        const Color c = face.hasColor ? face.color : Color{0.8f, 0.8f, 0.8f, 1};
        const std::vector<int> tri = m.TriangulateFace(fi);
        for (size_t k = 0; k + 2 < tri.size(); k += 3) {
            float sx[3], sy[3], d[3];
            float shade[3] = {lit, lit, lit};
            if (g_smooth) for (int i = 0; i < 3; ++i) shade[i] = 0.35f + 0.65f * std::max(0.0f, Vec3::Dot(cornerNormal(fi, face.verts[tri[k + i]]), light));
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
                    const float z = 1.0f / (l0 / d[0] + l1 / d[1] + l2 / d[2]);   // perspective-correct depth
                    const size_t idx = (size_t)y * im.w + x;
                    if (z >= im.z[idx]) continue;
                    im.z[idx] = z;
                    const float lit = g_smooth ? (l0 * shade[0] + l1 * shade[1] + l2 * shade[2]) : shade[0];
                    const float edge = g_edges && std::min({l0, l1, l2}) < 0.07f ? 0.45f : 1.0f;
                    im.px[idx * 3] = (uint8_t)std::min(255.0f, c.r * lit * 255 * edge);
                    im.px[idx * 3 + 1] = (uint8_t)std::min(255.0f, c.g * lit * 255 * edge);
                    im.px[idx * 3 + 2] = (uint8_t)std::min(255.0f, c.b * lit * 255 * edge);
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
    const float r = std::max(g_zoom < 2.0f ? 0.03f : 0.5f, (hi - lo).Magnitude() * 0.5f);
    Cam cam;
    cam.target = c;
    cam.eye = c + Vec3{std::sin(yaw) * std::cos(g_elev), std::sin(g_elev), std::cos(yaw) * std::cos(g_elev)} * (r * g_zoom);
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

    if (argc > 3 && std::string(argv[3]) == "more") {   // quick run: the newer templates, a sheet each (and a validity line)
        g_smooth = true;
        for (const char* id : {"aircraft", "boat", "weapon", "tower", "lamp", "tent"}) {
            const Template& A = *Registry::Instance().Find(id);
            const int cols = 4, tw = 360, th = 300;
            const int rows = ((int)A.starts.size() + cols - 1) / cols;
            Img sheet(cols * tw, rows * th);
            sheet.Clear(40, 42, 48);
            for (int i = 0; i < (int)A.starts.size(); ++i) {
                Recipe r = DefaultRecipe(A, A.starts[i].id);
                r.params["detail"] = 2;
                const PolyMesh m = Create(r);
                Scene(sheet, m, (i % cols) * tw + 2, (i / cols) * th + 2, tw - 4, th - 4, std::string(id) == "weapon" ? 0.5f : 0.75f);
            }
            Save(dir, std::string("more_") + id + ".png", sheet);
        }
        return 0;
    }
    if (argc > 3 && std::string(argv[3]) == "humans") {   // quick run: front, side and 3/4 of one or more designs at Low, Medium, High, Ultra
        const Template& H = *Registry::Instance().Find("human");
        std::vector<std::string> ids;
        for (int i = 4; i < argc; ++i) ids.push_back(argv[i]);
        if (ids.empty()) ids = {"average"};
        g_smooth = true; g_elev = 0.08f;
        if (ids[0] == "all") {   // a sheet of every start design at one Detail level (default High): front 3/4 view
            const int lv = ids.size() > 1 ? std::atoi(ids[1].c_str()) : 2;
            const int cols = 6, tw = 250, th = 470;
            const int rows = ((int)H.starts.size() + cols - 1) / cols;
            Img sheet(cols * tw, rows * th);
            sheet.Clear(40, 42, 48);
            for (int i = 0; i < (int)H.starts.size(); ++i) {
                Recipe r = DefaultRecipe(H, H.starts[i].id);
                r.params["detail"] = lv;
                Scene(sheet, Create(r), (i % cols) * tw + 2, (i / cols) * th + 2, tw - 4, th - 4, 0.45f);
            }
            Save(dir, "human_all_detail" + std::to_string(lv) + (argc > 2 ? std::string(argv[2]) : std::string()) + ".png", sheet);
            return 0;
        }
        const std::string tag = argc > 2 ? std::string(argv[2]) : "";
        for (const std::string& id : ids) {
            Img im(4 * 330, 3 * 560);
            im.Clear(40, 42, 48);
            for (int lv = 0; lv < 4; ++lv) {
                Recipe r = DefaultRecipe(H, id);
                r.params["detail"] = lv;
                const PolyMesh m = Create(r);
                std::printf("human %s detail %d: %d triangles\n", id.c_str(), lv, TriangleCount(m));
                g_smooth = lv >= 1;
                for (int v = 0; v < 3; ++v) Scene(im, m, lv * 330 + 2, v * 560 + 2, 326, 556, v == 0 ? 0.0f : v == 1 ? 1.5708f : 0.6f);
            }
            Save(dir, "human_views_" + id + tag + ".png", im);
            // close-ups at High: the head from three sides, a hand, a foot and the shoulder, each framed on its own parts
            Recipe r = DefaultRecipe(H, id);
            r.params["detail"] = 2;
            const std::vector<GeneratedPart> gp = Generate(r);
            auto pick = [&](std::initializer_list<const char*> slots, const char* prefix = nullptr) {
                PolyMesh m;
                for (const GeneratedPart& p : gp) {
                    bool ok = false;
                    for (const char* sl : slots) if (p.slot == sl) ok = true;
                    if (prefix && p.id.rfind(prefix, 0) == 0) ok = true;
                    if (ok) shapes::Append(m, p.mesh);
                }
                return m;
            };
            Img cl(5 * 360, 2 * 420);
            cl.Clear(40, 42, 48);
            g_smooth = true; g_elev = 0.05f; g_zoom = 1.7f;
            const PolyMesh head = pick({"head", "eyes", "eyewhite", "brows", "nose", "mouth", "ears", "hair", "neck", "face"});
            Scene(cl, head, 2, 2, 356, 416, 0.0f);
            Scene(cl, head, 362, 2, 356, 416, 1.5708f);
            Scene(cl, head, 722, 2, 356, 416, 0.7f);
            g_zoom = 1.4f;
            { PolyMesh hnd; for (const GeneratedPart& p : gp) if (p.id == "hand/1") shapes::Append(hnd, p.mesh); Scene(cl, hnd, 1082, 2, 356, 416, 1.5f); }
            { PolyMesh ft; for (const GeneratedPart& p : gp) if (p.id == "foot/1") shapes::Append(ft, p.mesh); Scene(cl, ft, 1442, 2, 356, 416, 1.0f); }
            g_zoom = 2.1f;
            g_elev = 0.35f; g_zoom = 2.1f;
            const PolyMesh shoulder = pick({"torso", "arm", "neck", "hand"});
            Scene(cl, shoulder, 2, 422, 356, 416, 0.0f);
            Scene(cl, shoulder, 362, 422, 356, 416, 0.8f);
            Scene(cl, pick({"leg", "foot", "pelvis"}), 722, 422, 356, 416, 0.7f);
            Scene(cl, pick({"torso", "pelvis"}), 1082, 422, 356, 416, 0.0f);
            Scene(cl, pick({"torso", "pelvis"}), 1442, 422, 356, 416, 2.6f);
            g_elev = 0.08f;
            Save(dir, "human_close_" + id + tag + ".png", cl);
        }
        return 0;
    }
    if (argc > 3 && (std::string(argv[3]) == "cars" || std::string(argv[3]) == "bikes")) {   // quick run: only the silhouette and 3/4 sheets
        const bool bikes = std::string(argv[3]) == "bikes";
        const std::string stem = bikes ? "motorcycles" : "cars";
        const Template& C = *Registry::Instance().Find(bikes ? "motorcycle" : "car");
        const int cols = 3, tw = 440, th = 220;
        const int rows = ((int)C.starts.size() + cols - 1) / cols;
        Img side(cols * tw, rows * th), q3(cols * tw, rows * 280);
        side.Clear(40, 42, 48); q3.Clear(40, 42, 48);
        for (int i = 0; i < (int)C.starts.size(); ++i) {
            const PolyMesh m = Create(DefaultRecipe(C, C.starts[i].id));
            Scene(side, m, (i % cols) * tw + 4, (i / cols) * th + 4, tw - 8, th - 8, 0.0f);
            Scene(q3, m, (i % cols) * tw + 4, (i / cols) * 280 + 4, tw - 8, 272, 0.7f);
        }
        Save(dir, stem + "_side_views.png", side);
        Save(dir, stem + "_three_quarter.png", q3);
        return 0;
    }
    if (argc > 3 && std::string(argv[3]) == "envprops") {   // quick run: one sheet per category with every start design
        for (const char* cat : {"Environment", "Props"}) {
            std::vector<std::pair<const Template*, size_t>> items;
            for (const Template* tp : Registry::Instance().InCategory(cat)) for (size_t i = 0; i < tp->starts.size(); ++i) items.push_back({tp, i});
            const int cols = 8, tw = 200, th = 190, rows = ((int)items.size() + cols - 1) / cols;
            Img sheet(cols * tw, rows * th);
            sheet.Clear(40, 42, 48);
            for (int k = 0; k < (int)items.size(); ++k)
                Scene(sheet, Create(DefaultRecipe(*items[(size_t)k].first, items[(size_t)k].first->starts[items[(size_t)k].second].id)), (k % cols) * tw + 3, (k / cols) * th + 3, tw - 6, th - 6, 0.6f);
            Save(dir, std::string(std::string(cat) == "Props" ? "props" : "environment") + "_overview.png", sheet);
        }
        return 0;
    }
    if (argc > 3 && std::string(argv[3]) == "chars") {   // quick run: the character styles and the hairstyles
        const Template& C = *Registry::Instance().Find("human");
        const char* names[4] = {"realistic", "stylised", "cartoon", "blocky"};
        {
            const int tw = 330, th = 480;
            Img sheet(4 * tw, 2 * th);
            sheet.Clear(40, 42, 48);
            for (int st = 0; st < 4; ++st)
                for (int row = 0; row < 2; ++row) {
                    Recipe r = DefaultRecipe(C, "adventurer");
                    r.params["style"] = st; r.params["detail"] = row == 0 ? 1.0 : 2.0;
                    Scene(sheet, Create(r), st * tw + 4, row * th + 4, tw - 8, th - 8, 0.45f);
                }
            Save(dir, "characters_styles.png", sheet);
        }
        {
            const SlotDef* hs = nullptr;
            for (const SlotDef& sl : C.slots) if (sl.id == "hair") hs = &sl;
            const int cols = 5, tw = 250, th = 250, n = (int)hs->designs.size(), rows = (n + cols - 1) / cols;
            Img sheet(cols * tw, rows * th);
            sheet.Clear(40, 42, 48);
            for (int i = 0; i < n; ++i) {
                Recipe r = DefaultRecipe(C, "average");
                r.slotDesign["hair"] = hs->designs[(size_t)i].id; r.params["detail"] = 1.0;
                PolyMesh head;
                for (const GeneratedPart& gp : Generate(r))
                    if (gp.id.rfind("head/", 0) == 0 || gp.id.rfind("hair/", 0) == 0 || gp.id.rfind("neck/", 0) == 0 || gp.id.rfind("eyes/", 0) == 0 || gp.id.rfind("nose/", 0) == 0 || gp.id.rfind("mouth/", 0) == 0 || gp.id.rfind("brows/", 0) == 0 || gp.id.rfind("ears/", 0) == 0 || gp.id.rfind("face/", 0) == 0) shapes::Append(head, gp.mesh);
                Scene(sheet, head, (i % cols) * tw + 4, (i / cols) * th + 4, tw - 8, th - 8, 2.3f);
            }
            Save(dir, "characters_hairstyles.png", sheet);
        }
        (void)names;
        return 0;
    }
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
    // 5. Architecture: a contact sheet per template, and a building with a window, the door and the roof swapped.
    {
        for (const char* id : {"wall", "stairs", "door", "window", "roof", "building"}) {
            const Template& A = *Registry::Instance().Find(id);
            const int cols = 3, tw = 400, th = 280;
            const int rows = ((int)A.starts.size() + cols - 1) / cols;
            Img sheet(cols * tw, rows * th);
            sheet.Clear(40, 42, 48);
            for (int i = 0; i < (int)A.starts.size(); ++i) {
                const PolyMesh m = Create(DefaultRecipe(A, A.starts[i].id));
                Scene(sheet, m, (i % cols) * tw + 4, (i / cols) * th + 4, tw - 8, th - 8, 0.7f);
            }
            Save(dir, std::string("architecture_") + id + ".png", sheet);
        }
        const Template& B = *Registry::Instance().Find("building");
        const Recipe base = DefaultRecipe(B, "townhouse");
        Recipe swapped = base;
        swapped.slotDesign["roof"] = "gable";
        swapped.slotDesign["door"] = "panelled";
        swapped.partDesign["window/0/0/0"] = "colonial";
        swapped.partDesign["window/1/0/1"] = "colonial";
        Img im(1200, 420);
        im.Clear(0, 0, 0);
        Scene(im, Create(base), 0, 0, 600, 420, 0.6f);
        Scene(im, Regenerate(Create(base), base, swapped).mesh, 600, 0, 600, 420, 0.6f);
        Save(dir, "building_part_swap_before_after.png", im);
    }
    // 6. Vehicles: a contact sheet per type, one picture per design.
    for (const char* id : {"car", "truck", "bus", "motorcycle", "trailer"}) {
        const Template& A = *Registry::Instance().Find(id);
        const int cols = 3, tw = 400, th = 260;
        const int rows = ((int)A.starts.size() + cols - 1) / cols;
        Img sheet(cols * tw, rows * th);
        sheet.Clear(40, 42, 48);
        for (int i = 0; i < (int)A.starts.size(); ++i) {
            const PolyMesh m = Create(DefaultRecipe(A, A.starts[i].id));
            Scene(sheet, m, (i % cols) * tw + 4, (i / cols) * th + 4, tw - 8, th - 8, 0.8f);
            Img one(640, 360);
            Scene(one, m, 0, 0, 640, 360, 0.8f);
            Save(dir, std::string("vehicle_") + id + "_" + A.starts[i].id + ".png", one);
        }
        Save(dir, std::string("vehicles_") + id + ".png", sheet);
    }
    // 6b. Characters: a contact sheet per template, and one picture per design (seen from the front-left).
    for (const char* id : {"human", "robot", "creature"}) {
        const Template& A = *Registry::Instance().Find(id);
        const int cols = 4, tw = 300, th = 340;
        const int rows = ((int)A.starts.size() + cols - 1) / cols;
        Img sheet(cols * tw, rows * th);
        sheet.Clear(40, 42, 48);
        for (int i = 0; i < (int)A.starts.size(); ++i) {
            const PolyMesh m = Create(DefaultRecipe(A, A.starts[i].id));
            Scene(sheet, m, (i % cols) * tw + 4, (i / cols) * th + 4, tw - 8, th - 8, 0.5f);
            Img one(480, 540);
            Scene(one, m, 0, 0, 480, 540, 0.5f);
            Save(dir, std::string("character_") + id + "_" + A.starts[i].id + ".png", one);
        }
        Save(dir, std::string("characters_") + id + ".png", sheet);
    }
    // 7. Detail levels: the same model at Low, Medium, High and Ultra, with the triangle count.
    {
        struct Item { const char* tpl; const char* start; const char* name; float yaw; };
        for (const Item& it : {Item{"car", "sports", "car", 0.8f}, Item{"fence", "wrought_iron", "fence", 0.55f}, Item{"building", "townhouse", "building", 0.6f},
                               Item{"motorcycle", "cruiser", "motorcycle", 0.9f}, Item{"human", "adventurer", "human", 0.5f}}) {
            const Template& A = *Registry::Instance().Find(it.tpl);
            Img im(1600, 360);
            im.Clear(0, 0, 0);
            g_edges = true;
            std::string counts;
            for (int lv = 0; lv < 4; ++lv) {
                Recipe r = DefaultRecipe(A, it.start);
                r.params["detail"] = lv;
                const PolyMesh m = Create(r);
                Scene(im, m, lv * 400, 0, 400, 360, it.yaw);
                counts += " " + std::to_string(TriangleCount(m));
            }
            g_edges = false;
            Save(dir, std::string("detail_") + it.name + ".png", im);
            std::printf("detail %s triangles (Low Medium High Ultra):%s\n", it.name, counts.c_str());
        }
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
        std::ofstream(ex + "/guided_fences.okayscene", std::ios::binary) << scene;
        std::printf("wrote the example folder %s (%d fences)\n", ex.c_str(), (int)items.size());

        // The architecture examples: every start design of every architecture template, a row per template.
        std::string arch = scene.substr(0, scene.find("gameobject 1 ")) +
            "gameobject 1 \"Ground\"\n  active 1\n  parent -1\n  transform 40 -0.5 40 0 0 0 1 200 1 200\n"
            "  mesh \"Cube\" 0.32941177 0.5019608 0.27450982 1 0 \"\" 0\n  material 0 0 0 0 16 0 \"\" 1 1\nend\n";
        arch.replace(arch.find("name \"Guided fences\""), 21, "name \"Guided architecture\"");
        int aid = 2, row = 0, total = 0;
        for (const char* tid : {"wall", "stairs", "door", "window", "roof", "building"}) {
            const Template& A = *Registry::Instance().Find(tid);
            for (size_t i = 0; i < A.starts.size(); ++i) {
                const std::string base = std::string(tid) + "_" + A.starts[i].id;
                const Recipe r = DefaultRecipe(A, A.starts[i].id);
                Create(r).Save(ex + "/" + base + ".okaymesh");
                SaveRecipe(SidecarPath(ex + "/" + base + ".okaymesh"), r);
                char tr[160];
                std::snprintf(tr, sizeof tr, "  transform %g 0 %g 0 0 0 1 1 1 1\n", (double)i * 12.0, (double)row * 14.0);
                arch += "gameobject " + std::to_string(aid++) + " \"" + A.name + " " + A.starts[i].name + "\"\n  active 1\n  parent -1\n" + tr +
                        "  mesh \"\" 1 1 1 1 0 \"" + base + ".okaymesh\" 0\n  material 0 0 0 0 16 0 \"\" 1 1\nend\n";
                ++total;
            }
            ++row;
        }
        arch += CameraRecord(aid++, 45.0, 40.0, 160.0);
        std::ofstream(ex + "/guided_architecture.okayscene", std::ios::binary) << arch;

        // The vehicle examples: every start design of every vehicle type.
        std::string veh = scene.substr(0, scene.find("gameobject 1 ")) +
            "gameobject 1 \"Ground\"\n  active 1\n  parent -1\n  transform 40 -0.5 40 0 0 0 1 200 1 200\n"
            "  mesh \"Cube\" 0.32941177 0.5019608 0.27450982 1 0 \"\" 0\n  material 0 0 0 0 16 0 \"\" 1 1\nend\n";
        veh.replace(veh.find("name \"Guided fences\""), 21, "name \"Guided vehicles\"");
        int vid = 2, vrow = 0, vtotal = 0;
        for (const char* tid : {"car", "truck", "bus", "motorcycle", "trailer"}) {
            const Template& A = *Registry::Instance().Find(tid);
            for (size_t i = 0; i < A.starts.size(); ++i) {
                const std::string base = std::string(tid) + "_" + A.starts[i].id;
                const Recipe r = DefaultRecipe(A, A.starts[i].id);
                Create(r).Save(ex + "/" + base + ".okaymesh");
                SaveRecipe(SidecarPath(ex + "/" + base + ".okaymesh"), r);
                char tr[160];
                std::snprintf(tr, sizeof tr, "  transform %g 0 %g 0 0 0 1 1 1 1\n", (double)i * 16.0, (double)vrow * 8.0);
                veh += "gameobject " + std::to_string(vid++) + " \"" + A.name + " " + A.starts[i].name + "\"\n  active 1\n  parent -1\n" + tr +
                       "  mesh \"\" 1 1 1 1 0 \"" + base + ".okaymesh\" 0\n  material 0 0 0 0 16 0 \"\" 1 1\nend\n";
                ++vtotal;
            }
            ++vrow;
        }
        veh += CameraRecord(vid++, 80.0, 50.0, 190.0);
        std::ofstream(ex + "/guided_vehicles.okayscene", std::ios::binary) << veh;
        std::printf("wrote %d vehicle models\n", vtotal);

        {   // Car body styles side by side at the same size, so only the shape differs.
            std::string cs = scene.substr(0, scene.find("gameobject 1 ")) +
                "gameobject 1 \"Ground\"\n  active 1\n  parent -1\n  transform 20 -0.5 0 0 0 0 1 80 1 40\n"
                "  mesh \"Cube\" 0.32941177 0.5019608 0.27450982 1 0 \"\" 0\n  material 0 0 0 0 16 0 \"\" 1 1\nend\n";
            cs.replace(cs.find("name \"Guided fences\""), 21, "name \"Guided car styles\"");
            const Template& C = *Registry::Instance().Find("car");
            int cid = 2, ci = 0;
            for (const char* st : {"sedan", "hatchback", "coupe", "suv", "pickup", "sports", "van"}) {
                Recipe r = DefaultRecipe(C, st);
                r.params["length"] = 4.4; r.params["height"] = 1.45; r.params["width"] = 1.8; r.params["groundClear"] = 0.18; r.params["wheelbase"] = 2.7; r.params["wheelRadius"] = 0.33;
                const std::string base = std::string("carstyle_") + st;
                Create(r).Save(ex + "/" + base + ".okaymesh");
                SaveRecipe(SidecarPath(ex + "/" + base + ".okaymesh"), r);
                char tr[160];
                std::snprintf(tr, sizeof tr, "  transform %g 0 0 0 0 0 1 1 1 1\n", (double)ci * 6.0);
                cs += "gameobject " + std::to_string(cid++) + " \"Car " + st + "\"\n  active 1\n  parent -1\n" + tr +
                      "  mesh \"\" 1 1 1 1 0 \"" + base + ".okaymesh\" 0\n  material 0 0 0 0 16 0 \"\" 1 1\nend\n";
                ++ci;
            }
            cs += CameraRecord(cid++, 18.0, 5.0, 24.0);
            std::ofstream(ex + "/guided_car_styles.okayscene", std::ios::binary) << cs;
            std::printf("wrote the car styles example\n");
        }

        // Environment and prop examples: every start design of every template, a row per template, with a Main Camera in front.
        for (const char* cat : {"Environment", "Props"}) {
            const bool env = std::string(cat) == "Environment";
            const double sp = env ? 16.0 : 3.0, rowSp = env ? 22.0 : 3.5;
            std::string sc = scene.substr(0, scene.find("gameobject 1 ")) +
                "gameobject 1 \"Ground\"\n  active 1\n  parent -1\n  transform 40 -0.5 40 0 0 0 1 400 1 400\n"
                "  mesh \"Cube\" 0.32941177 0.5019608 0.27450982 1 0 \"\" 0\n  material 0 0 0 0 16 0 \"\" 1 1\nend\n";
            sc.replace(sc.find("name \"Guided fences\""), 21, std::string("name \"Guided ") + (env ? "environment" : "props") + "\"");
            int gid = 2, grow = 0, gtotal = 0, maxCols = 0;
            for (const Template* tp : Registry::Instance().InCategory(cat)) {
                const Template& A = *tp;
                for (size_t i = 0; i < A.starts.size(); ++i) {
                    const std::string base = A.id + "_" + A.starts[i].id;
                    const Recipe r = DefaultRecipe(A, A.starts[i].id);
                    Create(r).Save(ex + "/" + base + ".okaymesh");
                    SaveRecipe(SidecarPath(ex + "/" + base + ".okaymesh"), r);
                    char tr[160];
                    std::snprintf(tr, sizeof tr, "  transform %g 0 %g 0 0 0 1 1 1 1\n", (double)i * sp, (double)grow * rowSp);
                    sc += "gameobject " + std::to_string(gid++) + " \"" + A.name + " " + A.starts[i].name + "\"\n  active 1\n  parent -1\n" + tr +
                          "  mesh \"\" 1 1 1 1 0 \"" + base + ".okaymesh\" 0\n  material 0 0 0 0 16 0 \"\" 1 1\nend\n";
                    ++gtotal;
                    maxCols = std::max(maxCols, (int)i + 1);
                }
                ++grow;
            }
            const double width = std::max(1, maxCols - 1) * sp, depth = std::max(1, grow - 1) * rowSp;
            sc += CameraRecord(gid++, width / 2, std::max(width, depth) * 0.45 + 2, depth + std::max(width, depth) * 1.1 + 6);
            std::ofstream(ex + (env ? "/guided_environment.okayscene" : "/guided_props.okayscene"), std::ios::binary) << sc;
            std::printf("wrote %d %s models\n", gtotal, cat);
        }

        // The character examples: every start design of human, robot and creature.
        std::string chr = scene.substr(0, scene.find("gameobject 1 ")) +
            "gameobject 1 \"Ground\"\n  active 1\n  parent -1\n  transform 10 -0.5 10 0 0 0 1 100 1 100\n"
            "  mesh \"Cube\" 0.32941177 0.5019608 0.27450982 1 0 \"\" 0\n  material 0 0 0 0 16 0 \"\" 1 1\nend\n";
        chr.replace(chr.find("name \"Guided fences\""), 21, "name \"Guided characters\"");
        int cid = 2, crow = 0, ctotal = 0;
        for (const char* tid : {"human", "robot", "creature"}) {
            const Template& A = *Registry::Instance().Find(tid);
            for (size_t i = 0; i < A.starts.size(); ++i) {
                const std::string base = std::string(tid) + "_" + A.starts[i].id;
                const Recipe r = DefaultRecipe(A, A.starts[i].id);
                Create(r).Save(ex + "/" + base + ".okaymesh");
                SaveRecipe(SidecarPath(ex + "/" + base + ".okaymesh"), r);
                char tr[160];
                std::snprintf(tr, sizeof tr, "  transform %g 0 %g 0 0 0 1 1 1 1\n", (double)i * 4.0, (double)crow * 5.0);
                chr += "gameobject " + std::to_string(cid++) + " \"" + A.name + " " + A.starts[i].name + "\"\n  active 1\n  parent -1\n" + tr +
                       "  mesh \"\" 1 1 1 1 0 \"" + base + ".okaymesh\" 0\n  material 0 0 0 0 16 0 \"\" 1 1\nend\n";
                ++ctotal;
            }
            ++crow;
        }
        chr += CameraRecord(cid++, 22.0, 7.0, 36.0);
        std::ofstream(ex + "/guided_characters.okayscene", std::ios::binary) << chr;
        std::printf("wrote %d character models\n", ctotal);
        // The newer templates (aircraft, boat, weapon, tower, lamp, tent) and the three humans at High detail: a row each,
        // models spaced by their own size so a 36 m airliner and a 0.3 m pistol both sit in view of the scene camera.
        {
            auto makeScene = [&](const std::string& name, double gx, double gz, double gsize) {
                std::string sc = scene.substr(0, scene.find("gameobject 1 ")) +
                    "gameobject 1 \"Ground\"\n  active 1\n  parent -1\n  transform " + std::to_string(gx) + " -0.5 " + std::to_string(gz) + " 0 0 0 1 " + std::to_string(gsize) + " 1 " + std::to_string(gsize) + "\n"
                    "  mesh \"Cube\" 0.32941177 0.5019608 0.27450982 1 0 \"\" 0\n  material 0 0 0 0 16 0 \"\" 1 1\nend\n";
                sc.replace(sc.find("name \"Guided fences\""), 21, "name \"" + name + "\"");
                return sc;
            };
            auto place = [&](std::string& sc, int& oid, const std::string& base, const std::string& title, const Recipe& r, double& cursor, double z) {
                const PolyMesh m = Create(r);
                m.Save(ex + "/" + base + ".okaymesh");
                SaveRecipe(SidecarPath(ex + "/" + base + ".okaymesh"), r);
                Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
                for (const Vec3& v : m.verts) { lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)}; hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)}; }
                const double w = std::max(0.5, (double)(hi.x - lo.x)), gap = std::max(0.6, w * 0.25);
                char tr[200];
                std::snprintf(tr, sizeof tr, "  transform %g 0 %g 0 0 0 1 1 1 1\n", cursor + w * 0.5 - (double)(lo.x + hi.x) * 0.5 + (double)lo.x * 0.0, z);
                sc += "gameobject " + std::to_string(oid++) + " \"" + title + "\"\n  active 1\n  parent -1\n" + tr + "  mesh \"\" 1 1 1 1 0 \"" + base + ".okaymesh\" 0\n  material 0 0 0 0 16 0 \"\" 1 1\nend\n";
                cursor += w + gap;
            };
            // one row per template, the rows laid out in z (away from the camera)
            {
                std::string sc2 = makeScene("Guided more models", 0, 0, 400);
                int oid = 2;
                double z = 0;
                const double rowDepth[6] = {1.5, 1.5, 4.0, 8.0, 10.0, 22.0};
                int ri = 0, total2 = 0;
                double widest = 0;
                for (const char* tid : {"weapon", "lamp", "tent", "tower", "boat", "aircraft"}) {
                    const Template& A = *Registry::Instance().Find(tid);
                    double cursor = 0;
                    z -= rowDepth[ri++];
                    for (const StartDesign& st : A.starts) { place(sc2, oid, std::string(tid) + "_" + st.id, A.name + " " + st.name, DefaultRecipe(A, st.id), cursor, z); ++total2; }
                    widest = std::max(widest, cursor);
                    z -= rowDepth[ri - 1];
                }
                sc2 += CameraRecord(oid++, widest * 0.5, std::max(8.0, widest * 0.35), 30.0 + widest * 0.55);
                std::ofstream(ex + "/guided_more.okayscene", std::ios::binary) << sc2;
                std::printf("wrote %d models of the newer templates\n", total2);
            }
            // three humans at High detail
            {
                std::string sc3 = makeScene("Guided humans, High detail", 3.0, 0, 40);
                int oid = 2;
                double cursor = 0;
                for (const char* id : {"woman", "man", "elder"}) {
                    Recipe r = DefaultRecipe(*Registry::Instance().Find("human"), id);
                    r.params["detail"] = 2;
                    place(sc3, oid, std::string("human_high_") + id, std::string("Human ") + id + " (High)", r, cursor, 0.0);
                }
                sc3 += CameraRecord(oid++, 3.0, 1.6, 6.5);
                std::ofstream(ex + "/guided_humans_high.okayscene", std::ios::binary) << sc3;
                std::printf("wrote 3 humans at High detail\n");
            }
        }
        std::printf("wrote %d architecture models\n", total);
    }
    return 0;
}
