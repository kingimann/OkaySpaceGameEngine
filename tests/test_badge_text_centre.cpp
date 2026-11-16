// The level badge of the nameplate template (UIGallery::AddNameplate, a UITemplates Badge: a pill panel with a
// centred Text child) draws its text in the middle of the pill, in world space (a nameplate on a character) and in
// screen space (a Canvas child), and a long bound level ("Lv 120") stays inside the pill.
//
// How it measures: the scene is drawn with the engine's UI pass (RenderSceneUI, the same pass the player uses, with
// the player's world-space projector) on a small CPU painter that keeps the pixels. The text's pixels are what
// changes when only the label is made invisible; the pill's pixels are what changes when only the pill is made
// invisible. So the test reads where things really landed on screen, whatever the font.
// Set OKAY_SHOT_DIR to a folder to also save the pictures (badge_world.png, badge_screen.png).
// Written for this engine from the UI renderer's own interface; no other project's code was used.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UIGallery.hpp"
#include "okay/Testing/Regression.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace okay;
using namespace okay::testing;

namespace {

// A painter on an okay::Image: source-over blending, float rects weighted by how much of each pixel they cover,
// solid triangles, and font-atlas copies (the atlas is the TtfFont's own image: white, coverage in alpha).
struct ImagePainter : ui2d::Painter {
    Image& img;
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
    std::uint8_t modR = 255, modG = 255, modB = 255, modA = 255;
    bool clipOn = false;
    ui2d::Rect clip{0, 0, 0, 0};
    float sx = 1.0f, sy = 1.0f;
    explicit ImagePainter(Image& target) : img(target) {}

    bool Inside(int x, int y) const {
        if (x < 0 || y < 0 || x >= img.Width() || y >= img.Height()) return false;
        if (!clipOn) return true;
        return x >= clip.x && y >= clip.y && x < clip.x + clip.w && y < clip.y + clip.h;
    }
    void Blend(int x, int y, std::uint8_t cr, std::uint8_t cg, std::uint8_t cb, float alpha) {
        if (alpha <= 0.0f || !Inside(x, y)) return;
        if (alpha > 1.0f) alpha = 1.0f;
        std::uint8_t* p = img.Data() + ((std::size_t)y * img.Width() + x) * 4;
        p[0] = (std::uint8_t)std::lround(cr * alpha + p[0] * (1.0f - alpha));
        p[1] = (std::uint8_t)std::lround(cg * alpha + p[1] * (1.0f - alpha));
        p[2] = (std::uint8_t)std::lround(cb * alpha + p[2] * (1.0f - alpha));
        p[3] = 255;
    }
    // Fill a float rect (already in pixels), each pixel weighted by its covered area.
    void Cover(float x, float y, float w, float h, std::uint8_t cr, std::uint8_t cg, std::uint8_t cb, float alpha) {
        if (w <= 0.0f || h <= 0.0f || alpha <= 0.0f) return;
        const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
        const int x1 = (int)std::ceil(x + w), y1 = (int)std::ceil(y + h);
        for (int py = y0; py < y1; ++py) {
            const float cy = std::min(y + h, (float)py + 1.0f) - std::max(y, (float)py);
            if (cy <= 0.0f) continue;
            for (int px = x0; px < x1; ++px) {
                const float cx = std::min(x + w, (float)px + 1.0f) - std::max(x, (float)px);
                if (cx > 0.0f) Blend(px, py, cr, cg, cb, alpha * cx * cy);
            }
        }
    }

    void SetDrawColor(std::uint8_t rr, std::uint8_t gg, std::uint8_t bb, std::uint8_t aa) override { r = rr; g = gg; b = bb; a = aa; }
    void FillRect(const ui2d::Rect* rc) override {
        if (!rc) { Cover(0.0f, 0.0f, (float)img.Width(), (float)img.Height(), r, g, b, a / 255.0f); return; }
        Cover(rc->x * sx, rc->y * sy, rc->w * sx, rc->h * sy, r, g, b, a / 255.0f);
    }
    void FillRectF(const ui2d::FRect* rc) override {
        if (rc) Cover(rc->x * sx, rc->y * sy, rc->w * sx, rc->h * sy, r, g, b, a / 255.0f);
    }
    void DrawRect(const ui2d::Rect* rc) override {
        if (!rc) return;
        const ui2d::Rect e[4] = {{rc->x, rc->y, rc->w, 1}, {rc->x, rc->y + rc->h - 1, rc->w, 1},
                                 {rc->x, rc->y, 1, rc->h}, {rc->x + rc->w - 1, rc->y, 1, rc->h}};
        for (const auto& q : e) FillRect(&q);
    }
    void DrawLine(int x0, int y0, int x1, int y1) override {
        const int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
        for (int i = 0; i <= n; ++i) {
            const float t = n ? (float)i / (float)n : 0.0f;
            DrawPoint((int)std::lround(x0 + (x1 - x0) * t), (int)std::lround(y0 + (y1 - y0) * t));
        }
    }
    void DrawPoint(int x, int y) override { const ui2d::Rect q{x, y, 1, 1}; FillRect(&q); }
    void SetClipRect(const ui2d::Rect* rc) override {
        clipOn = rc != nullptr;
        if (rc) clip = ui2d::Rect{(int)std::floor(rc->x * sx), (int)std::floor(rc->y * sy),
                                  (int)std::ceil(rc->w * sx), (int)std::ceil(rc->h * sy)};
    }
    void GetClipRect(ui2d::Rect* rc) override { *rc = clipOn ? clip : ui2d::Rect{0, 0, 0, 0}; }
    bool IsClipEnabled() override { return clipOn; }
    void SetScale(float x, float y) override { sx = x; sy = y; }
    ui2d::Tex LoadTexture(const std::string&) override { return nullptr; }
    ui2d::Tex FontAtlas(TtfFont* f) override { return f ? (ui2d::Tex)&f->Atlas() : nullptr; }
    void QueryTexture(ui2d::Tex t, int* w, int* h) override {
        const Image* im = (const Image*)t;
        *w = im ? im->Width() : 0;
        *h = im ? im->Height() : 0;
    }
    void SetTextureColorMod(ui2d::Tex, std::uint8_t rr, std::uint8_t gg, std::uint8_t bb) override { modR = rr; modG = gg; modB = bb; }
    void SetTextureAlphaMod(ui2d::Tex, std::uint8_t aa) override { modA = aa; }
    void Copy(ui2d::Tex t, const ui2d::Rect* src, const ui2d::Rect* dst) override {
        if (!dst) return;
        const ui2d::FRect f{(float)dst->x, (float)dst->y, (float)dst->w, (float)dst->h};
        CopyF(t, src, &f);
    }
    // A glyph: each destination pixel samples the atlas's coverage (bilinear) and blends the tint.
    void CopyF(ui2d::Tex t, const ui2d::Rect* src, const ui2d::FRect* dst) override {
        const Image* im = (const Image*)t;
        if (!im || !im->Valid() || !dst || dst->w <= 0.0f || dst->h <= 0.0f) return;
        const ui2d::Rect s = src ? *src : ui2d::Rect{0, 0, im->Width(), im->Height()};
        const float dx = dst->x * sx, dy = dst->y * sy, dw = dst->w * sx, dh = dst->h * sy;
        auto cov = [&](int x, int y) -> float {
            x = std::clamp(x, s.x, s.x + s.w - 1);
            y = std::clamp(y, s.y, s.y + s.h - 1);
            return im->Data()[((std::size_t)y * im->Width() + x) * 4 + 3] / 255.0f;
        };
        for (int py = (int)std::floor(dy); py < (int)std::ceil(dy + dh); ++py)
            for (int px = (int)std::floor(dx); px < (int)std::ceil(dx + dw); ++px) {
                const float u = s.x + ((float)px + 0.5f - dx) / dw * s.w - 0.5f;
                const float v = s.y + ((float)py + 0.5f - dy) / dh * s.h - 0.5f;
                if (u < s.x - 0.5f || v < s.y - 0.5f || u > s.x + s.w - 0.5f || v > s.y + s.h - 0.5f) continue;
                const int ix = (int)std::floor(u), iy = (int)std::floor(v);
                const float fx = u - ix, fy = v - iy;
                const float c = cov(ix, iy) * (1 - fx) * (1 - fy) + cov(ix + 1, iy) * fx * (1 - fy) +
                                cov(ix, iy + 1) * (1 - fx) * fy + cov(ix + 1, iy + 1) * fx * fy;
                Blend(px, py, modR, modG, modB, c * modA / 255.0f);
            }
    }
    void CopyEx(ui2d::Tex t, const ui2d::Rect* src, const ui2d::Rect* dst, double, const ui2d::Point*, int) override { Copy(t, src, dst); }
    void CopyExF(ui2d::Tex t, const ui2d::Rect* src, const ui2d::FRect* dst, double, const ui2d::FPoint*, int) override { CopyF(t, src, dst); }
    // Solid triangles in the first vertex's colour (the nameplate draws none with a texture).
    void Geometry(ui2d::Tex, const ui2d::Vertex* v, int nv, const int* idx, int ni) override {
        if (!v || nv < 3) return;
        const int n = idx ? ni : nv;
        auto edge = [](ui2d::FPoint p, ui2d::FPoint q, float x, float y) { return (x - p.x) * (q.y - p.y) - (y - p.y) * (q.x - p.x); };
        for (int i = 0; i + 2 < n; i += 3) {
            const int ia = idx ? idx[i] : i, ib = idx ? idx[i + 1] : i + 1, ic = idx ? idx[i + 2] : i + 2;
            if (ia < 0 || ib < 0 || ic < 0 || ia >= nv || ib >= nv || ic >= nv) continue;
            ui2d::FPoint A{v[ia].position.x * sx, v[ia].position.y * sy}, B{v[ib].position.x * sx, v[ib].position.y * sy},
                         C{v[ic].position.x * sx, v[ic].position.y * sy};
            if (edge(A, B, C.x, C.y) < 0.0f) std::swap(B, C);
            const ui2d::RGBA col = v[ia].color;
            const int x0 = (int)std::floor(std::min({A.x, B.x, C.x})), x1 = (int)std::ceil(std::max({A.x, B.x, C.x}));
            const int y0 = (int)std::floor(std::min({A.y, B.y, C.y})), y1 = (int)std::ceil(std::max({A.y, B.y, C.y}));
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    const float px = x + 0.5f, py = y + 0.5f;
                    if (edge(A, B, px, py) >= 0.0f && edge(B, C, px, py) >= 0.0f && edge(C, A, px, py) >= 0.0f)
                        Blend(x, y, col.r, col.g, col.b, col.a / 255.0f);
                }
        }
    }
};

// The pixels where two pictures differ by more than `threshold` (largest channel difference), as a box.
struct Box {
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;   // inclusive pixel bounds
    bool Empty() const { return x1 < x0 || y1 < y0; }
    float Cx() const { return (x0 + x1 + 1) * 0.5f; }
    float Cy() const { return (y0 + y1 + 1) * 0.5f; }
    int W() const { return Empty() ? 0 : x1 - x0 + 1; }
    int H() const { return Empty() ? 0 : y1 - y0 + 1; }
};
Box DiffBox(const Image& a, const Image& b, int threshold) {
    Box box;
    if (a.Width() != b.Width() || a.Height() != b.Height()) return box;
    for (int y = 0; y < a.Height(); ++y)
        for (int x = 0; x < a.Width(); ++x) {
            const std::uint8_t* p = a.Data() + ((std::size_t)y * a.Width() + x) * 4;
            const std::uint8_t* q = b.Data() + ((std::size_t)y * b.Width() + x) * 4;
            int d = 0;
            for (int c = 0; c < 3; ++c) d = std::max(d, std::abs((int)p[c] - (int)q[c]));
            if (d <= threshold) continue;
            box.x0 = std::min(box.x0, x); box.y0 = std::min(box.y0, y);
            box.x1 = std::max(box.x1, x); box.y1 = std::max(box.y1, y);
        }
    return box;
}

// The projector the player installs for world-space UI (okay-player main.cpp): the main camera's WorldToScreen,
// and the camera's right axis for billboards.
void InstallWorld(Scene& s, int w, int h) {
    UIWorld().active = false;
    Camera* cam = s.mainCamera;
    if (!cam || !cam->gameObject) return;
    UIWorld().active = true;
    UIWorld().screenW = (float)w; UIWorld().screenH = (float)h;
    UIWorld().right = cam->gameObject->transform->Right();
    UIWorld().project = [cam, w, h](const Vec3& p, Vec2& out, float& depth) {
        return cam->WorldToScreen(p, (float)w, (float)h, out, &depth);
    };
}
void ClearWorld() {
    UIWorld().active = false;
    UIWorld().project = nullptr;
}

// Draw the scene: its meshes (world-space pictures) or a plain backdrop, then the UI pass on top.
Image Draw(Scene& s, int w, int h, bool world) {
    Image img;
    if (world) {
        SceneRenderOptions ro;
        ro.width = w; ro.height = h;
        ro.background = Color::FromBytes(122, 172, 222);
        RenderSceneToImage(s, img, ro);
    }
    if (!img.Valid()) { img = Image(w, h); img.Fill(Color::FromBytes(122, 172, 222, 255)); }
    if (world) InstallWorld(s, w, h);
    ImagePainter painter(img);
    ui2d::RenderSceneUI(s, painter, w, h);
    ClearWorld();
    return img;
}

struct Badge {
    UIPanel* pill = nullptr;
    TextRenderer* label = nullptr;
};
Badge BadgeOf(GameObject* plate) {
    Badge b;
    if (Transform* t = plate->transform->Find("SwatchPlate/SwatchLevel")) b.pill = t->gameObject->GetComponent<UIPanel>();
    if (Transform* t = plate->transform->Find("SwatchPlate/SwatchLevel/Label")) b.label = t->gameObject->GetComponent<TextRenderer>();
    return b;
}

// Where the badge's text and pill landed in a picture of the scene.
struct Measured {
    Box pill, text;
    std::string shown;
};
Measured Measure(Scene& s, const Badge& b, int w, int h, bool world) {
    Measured m;
    if (!b.pill || !b.label) return m;
    m.shown = b.label->text;
    const Image full = Draw(s, w, h, world);
    const Color textCol = b.label->color;
    b.label->color.a = 0.0f;                                     // the text alone disappears
    const Image noText = Draw(s, w, h, world);
    const Color fill = b.pill->color, border = b.pill->borderColor;
    b.pill->color.a = 0.0f; b.pill->borderColor.a = 0.0f;        // then the pill too
    const Image noPill = Draw(s, w, h, world);
    b.label->color = textCol; b.pill->color = fill; b.pill->borderColor = border;
    m.text = DiffBox(full, noText, 40);                          // the glyphs' ink (not their faint edge)
    m.pill = DiffBox(noText, noPill, 6);                         // the pill, its 1 px border included
    return m;
}

void Report(const char* what, const Measured& m) {
    std::printf("  %s \"%s\": pill x %d..%d y %d..%d (%dx%d)  text x %d..%d y %d..%d (%dx%d)  "
                "centre offset dx %.1f dy %.1f\n",
                what, m.shown.c_str(), m.pill.x0, m.pill.x1, m.pill.y0, m.pill.y1, m.pill.W(), m.pill.H(),
                m.text.x0, m.text.x1, m.text.y0, m.text.y1, m.text.W(), m.text.H(),
                m.text.Cx() - m.pill.Cx(), m.text.Cy() - m.pill.Cy());
}

// The text is centred in the pill (2 px) and lies inside it, clear of its edge.
void CheckCentred(const Measured& m) {
    CHECK(!m.pill.Empty() && !m.text.Empty());
    if (m.pill.Empty() || m.text.Empty()) return;
    CHECK_NEAR(m.text.Cx(), m.pill.Cx(), 2.0);
    CHECK_NEAR(m.text.Cy(), m.pill.Cy(), 2.0);
    CHECK(m.text.x0 >= m.pill.x0 + 2 && m.text.x1 <= m.pill.x1 - 2);   // inside, not touching the border
    CHECK(m.text.y0 >= m.pill.y0 + 2 && m.text.y1 <= m.pill.y1 - 2);
}

GameObject* MakeCharacter(Scene& s, const char* name, float x, int level, Color body) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = {x, 0.0f, 0.0f};
    g->AddComponent<Stats>()->level = level;
    GameObject* mesh = s.CreateGameObject("Body");
    mesh->transform->SetParent(g->transform, false);
    mesh->transform->localPosition = {0.0f, 0.8f, 0.0f};
    mesh->transform->localScale = {0.8f, 1.6f, 0.8f};
    auto* mr = mesh->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube(1.0f);
    mr->color = body;
    return g;
}

GameObject* Nameplate(Scene& s, GameObject* owner) {
    GameObject* plate = UIGallery::AddNameplate(s, true);
    plate->transform->SetParent(owner->transform, false);
    plate->transform->localPosition = {-0.9f, 2.3f, 0.0f};   // the 180 px card's top-left: centred over the head
    return plate;
}

void SaveShot(const Image& img, const char* file) {
    const char* dir = std::getenv("OKAY_SHOT_DIR");
    if (!dir || !*dir) return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string path = std::string(dir) + "/" + file;
    std::printf("  saved %s (%s)\n", path.c_str(), img.SavePNG(path) ? "ok" : "FAILED");
}

} // namespace

int main() {
    RUN_SUITE("badge_text_centre");
    ActionList::ResetVars();

    {   // World space: two characters with the real nameplate template, levels 4 and 120.
        Scene s("badges");
        s.physicsEnabled = false;
        GameObject* cam = s.CreateGameObject("Main Camera");
        auto* c = cam->AddComponent<Camera>();                      // a camera looks down its -Z: at the characters
        s.mainCamera = c;
        GameObject* goblin = MakeCharacter(s, "Goblin Scout", -1.2f, 4, Color::FromBytes(84, 160, 70));
        GameObject* orc = MakeCharacter(s, "Orc Chief", 1.2f, 120, Color::FromBytes(176, 88, 70));
        GameObject* goblinPlate = Nameplate(s, goblin);
        const Badge bg = BadgeOf(goblinPlate), bo = BadgeOf(Nameplate(s, orc));
        UIProgressBar* bar = nullptr;
        if (Transform* t = goblinPlate->transform->Find("SwatchPlate/Health")) bar = t->gameObject->GetComponent<UIProgressBar>();
        CHECK(bg.pill && bg.label && bo.pill && bo.label && bar);
        s.Start();
        goblin->GetComponent<Stats>()->level = 4;
        orc->GetComponent<Stats>()->level = 120;
        goblin->GetComponent<Stats>()->health = 30.0f;
        orc->GetComponent<Stats>()->health = 75.0f;
        for (int i = 0; i < 2; ++i) s.Update(0.016f);                // the bindings write the text and the bar
        s.mainCamera = c;
        if (bg.label && bo.label) {
            CHECK(bg.label->text == "Lv 4");
            CHECK(bo.label->text == "Lv 120");
        }
        const int W = 1280, H = 540;
        // Near (1.3 screen px per design px) and farther away (0.85): the plate scales with the distance.
        for (float camZ : {3.6f, 5.5f}) {
            cam->transform->localPosition = {0.0f, 1.5f, camZ};
            if (camZ < 4.0f) SaveShot(Draw(s, W, H, true), "badge_world.png");
            if (!bg.pill || !bo.pill || !bar) break;
            InstallWorld(s, W, H);
            const float k = UIScaleFor(bg.pill->gameObject, (float)W, (float)H);
            Vec2 po{0, 0}, ps{0, 0}, bo2{0, 0}, bs{0, 0};
            const bool pillOn = GetUIScreenRect(bg.pill->gameObject, (float)W, (float)H, po, ps);
            const bool barOn = GetUIScreenRect(bar->gameObject, (float)W, (float)H, bo2, bs);
            ClearWorld();
            std::printf("  world, camera at z %.1f: %.2f screen px per design px\n", camZ, k);
            CHECK(camZ < 4.0f ? (k > 1.2f && k < 1.4f) : (k > 0.75f && k < 0.95f));
            const Measured mg = Measure(s, bg, W, H, true), mo = Measure(s, bo, W, H, true);
            Report("world", mg);
            Report("world", mo);
            CheckCentred(mg);
            CheckCentred(mo);
            // The pill is drawn at the size and place the layout gives it (as clicks see it), not at design size.
            CHECK(pillOn);
            CHECK_NEAR(mg.pill.x0, po.x, 2.0);
            CHECK_NEAR(mg.pill.W(), ps.x, 2.0);
            CHECK_NEAR(mg.pill.H(), ps.y, 2.0);
            // So is the health bar: hide it and see what changes.
            const Image full = Draw(s, W, H, true);
            const Color back = bar->background, fill = bar->fill;
            bar->background.a = 0.0f; bar->fill.a = 0.0f;
            const Image noBar = Draw(s, W, H, true);
            bar->background = back; bar->fill = fill;
            const Box drawnBar = DiffBox(full, noBar, 6);
            std::printf("  world bar: drawn x %d..%d (%d wide), layout %.1f wide\n", drawnBar.x0, drawnBar.x1, drawnBar.W(), bs.x);
            CHECK(barOn);
            CHECK_NEAR(drawnBar.x0, bo2.x, 2.0);
            CHECK_NEAR(drawnBar.W(), bs.x, 2.0);
        }
    }

    {   // Screen space: the same template as a Canvas child, at the canvas's design size.
        Scene s("badges2d");
        s.physicsEnabled = false;
        GameObject* plate = UIGallery::AddNameplate(s, false);
        if (Canvas* cv = s.FindObjectOfType<Canvas>()) cv->scaleMode = Canvas::ScaleMode::ConstantPixelSize;
        const Badge b = BadgeOf(plate);
        CHECK(b.pill && b.label);
        s.Start();
        const int W = 200, H = 60;
        for (int level : {4, 120}) {
            ActionList::Vars()["level"] = (float)level;              // no owner: the global "level" variable
            for (int i = 0; i < 2; ++i) s.Update(0.016f);
            if (!b.label) break;
            CHECK(b.label->text == "Lv " + std::to_string(level));
            if (level == 120) SaveShot(Draw(s, W, H, false), "badge_screen.png");
            const Measured m = Measure(s, b, W, H, false);
            Report("screen", m);
            CheckCentred(m);
        }
        // For the record, not checked: with the scene's UI font set to the 8x8 pixel font ("pixel"), the badge's
        // width (7 px a letter + 22, made for the default font) is too small even for its own "Lv 7".
        s.uiFont = "pixel";
        for (int level : {7, 120}) {
            ActionList::Vars()["level"] = (float)level;
            for (int i = 0; i < 2; ++i) s.Update(0.016f);
            Report("screen, pixel font", Measure(s, b, W, H, false));
        }
    }

    ActionList::ResetVars();
    TEST_MAIN_RESULT();
}
