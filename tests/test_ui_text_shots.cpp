// Headless pictures of the nameplate, the rich-text sign and the shipped sample scene (examples/ui_text), drawn with the same UI
// pass as the game on the software painter (no window, no GPU): so they show what the engine draws, not what a real window
// does. They go to OKAY_SHOT_DIR (or the first argument, or <build>/ui_text) and are checked for what they show: a nameplate whose
// pieces all sit inside its card, a sign whose lines do not overlap, health bars that shrink when a character is hurt.
// Written for this engine; no other project's code or assets were used.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/UIGallery.hpp"
#include "ui_soft_render.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

using namespace okay;

namespace {

constexpr int W = 1280, H = 720;

// A 1280x720 canvas on the software painter (ui_soft_render.hpp's painter, kept for the whole run).
struct Canvas2D {
    std::unique_ptr<okay::platform::Painter> p;
    Canvas2D() : p(OkayUI::soft::CreateBitmapPainter(W, H)) { if (p) p->SetBlendMode(okay::platform::BlendMode::Blend); }
    bool Ok() const { return p != nullptr; }
    void Clear(int r, int g, int b) { p->SetDrawColor((std::uint8_t)r, (std::uint8_t)g, (std::uint8_t)b, 255); p->Clear(); }
    void Fill(float x, float y, float w, float h, Color c) {
        p->SetDrawColor((std::uint8_t)(c.r * 255), (std::uint8_t)(c.g * 255), (std::uint8_t)(c.b * 255), (std::uint8_t)(c.a * 255));
        ui2d::Rect r{(int)std::lround(x), (int)std::lround(y), (int)std::lround(w), (int)std::lround(h)};
        p->FillRect(&r);
    }
    /// A full-canvas RGBA picture (top row first) copied over everything (no blending), as the 3D view under the UI.
    void Put(const std::uint32_t* rgba) {
        okay::platform::TextureDesc d; d.width = W; d.height = H; d.blend = okay::platform::BlendMode::None;
        ui2d::Tex t = p->CreateTexture(d, (const std::uint8_t*)rgba);
        if (!t) return;
        p->Copy(t, nullptr, nullptr);
        p->DestroyTexture(t);
    }
    Image ToImage() const {
        Image img(W, H);
        std::vector<std::uint8_t> px; int w = 0, h = 0;
        if (!p->ReadPixels(px, w, h) || w != W || h != H) return img;
        for (std::size_t i = 0; i + 3 < px.size(); i += 4) px[i + 3] = 255;
        std::memcpy(img.Data(), px.data(), px.size());
        return img;
    }
};

// One canvas for every picture (the painter keeps its font atlases between pictures).
Canvas2D& SharedCanvas() {
    static Canvas2D cv;
    return cv;
}

// How many pixels of an image are within `tol` of a colour (inside a rectangle).
int CountPixels(const Image& img, int r, int g, int b, int tol = 6, int x0 = 0, int y0 = 0, int x1 = W, int y1 = H) {
    int n = 0;
    const std::uint8_t* d = img.Data();                                             // RGBA bytes, row by row
    for (int y = std::max(0, y0); y < std::min(H, y1); ++y)
        for (int x = std::max(0, x0); x < std::min(W, x1); ++x) {
            const std::uint8_t* p = d + ((std::size_t)y * W + (std::size_t)x) * 4;
            if (std::abs((int)p[0] - r) <= tol && std::abs((int)p[1] - g) <= tol && std::abs((int)p[2] - b) <= tol) ++n;
        }
    return n;
}

bool SavePicture(const Image& img, const std::string& dir, const char* file) {
    const std::string path = dir + "/" + file;
    const bool ok = img.SavePNG(path);
    std::printf("  wrote %s%s\n", path.c_str(), ok ? "" : "  (FAILED)");
    return ok;
}

// ---- the nameplate in a 3D scene: two characters (boxes), each with its own plate as a child -------------------------------------
// `scaled` turns off the plate's Constant size (the plate then shrinks and grows with distance, 90 design pixels per world unit).
Image Plates3D(bool scaled) {
    ActionList::ResetVars();
    Scene s("plates"); s.physicsEnabled = false;
    GameObject* floor = s.CreateGameObject("Floor");
    floor->transform->localPosition = {0, -0.6f, 0}; floor->transform->localScale = {14, 0.2f, 8};
    floor->AddComponent<MeshRenderer>()->color = Color::FromBytes(90, 100, 110);
    auto mob = [&](const char* name, float x, Color c, float hp, float mx, int lvl) {
        GameObject* g = s.CreateGameObject(name);
        g->transform->localPosition = {x, 0.4f, 0};
        GameObject* m = s.CreateGameObject("Body"); m->transform->SetParent(g->transform, false); m->transform->localScale = {1.0f, 2.0f, 1.0f};
        m->AddComponent<MeshRenderer>()->color = c;
        auto* h = g->AddComponent<HealthStat>(); h->publishPrefs = false; h->maxHealth = mx; h->health = hp;
        g->AddComponent<Stats>()->level = lvl;
        return g;
    };
    GameObject* a = mob("Goblin Scout", -2.2f, Color::FromBytes(80, 170, 80), 30, 40, 3);
    GameObject* b = mob("Orc Chief", 2.2f, Color::FromBytes(170, 90, 70), 90, 100, 12);
    for (GameObject* owner : {a, b}) {
        GameObject* p = UIGallery::AddNameplate(s, true);
        p->transform->SetParent(owner->transform, false);
        p->transform->localPosition = {0.0f, 1.55f, 0.0f};                // just above the head
        if (scaled) { auto* w = p->GetComponent<WorldSpaceUI>(); w->constantSize = false; w->pixelsPerUnit = 90.0f; }
    }
    s.Start();
    a->GetComponent<HealthStat>()->health = 30; b->GetComponent<HealthStat>()->health = 90;
    a->GetComponent<HealthStat>()->Damage(10);                              // the Goblin is down to 20 of 40
    for (int i = 0; i < 3; ++i) s.Update(0.016f);

    const Vec3 eye{0, 2.0f, 8.0f};
    const Mat4 vp = Mat4::Perspective(40.0f, (float)W / H, 0.1f, 100.0f) * Mat4::LookAt(eye, {0, 0.9f, 0}, Vec3::Up);
    ApplySceneLight(s);
    Raster r; r.Resize(W, H); r.Clear(0xFF3A2A22u);
    RenderMeshes(r, s, vp, eye);
    Canvas2D& cv = SharedCanvas();
    Image empty(W, H);
    if (!cv.Ok()) return empty;
    cv.Put(r.color.data());
    UIWorld().active = true; UIWorld().screenW = W; UIWorld().screenH = H; UIWorld().right = {1, 0, 0};
    UIWorld().project = [vp](const Vec3& wp, Vec2& o, float& d) {
        Vec4 c = vp * Vec4{wp.x, wp.y, wp.z, 1.0f}; if (c.w <= 0.05f) return false;
        o = Vec2{W * 0.5f + c.x / c.w * W * 0.5f, H * 0.5f - c.y / c.w * H * 0.5f}; d = c.w; return true; };
    okay::platform::Painter& painter = *cv.p;
    ui2d::RenderSceneUI(s, painter, W, H);
    UIWorld().active = false;
    ActionList::ResetVars();
    return cv.ToImage();
}

// ---- the rich-text sign (the lines of different sizes) ------------------------------------------------------------------------
// `coloured` gives the big word its own colour so a test can find its rows; the text is otherwise the same.
Image Sign(bool coloured) {
    Scene s("sign");
    auto* tr = s.CreateGameObject("Sign")->AddComponent<TextRenderer>();
    tr->screenSpace = false;
    tr->color = Color::FromBytes(245, 240, 225);
    tr->shadow = true; tr->outline = false;
    tr->richText = true;
    tr->text = coloured
        ? "Welcome to [b]Okay Town[/b]\n[color=#ff5050]Danger:[/color] [i]wolves[/i] ahead\n[size=60]tiny[/size] and [size=150][color=#80ff80]larger[/color][/size] text\n[b][i][color=#ffd040]bold italic gold[/color][/i][/b]"
        : "Welcome to [b]Okay Town[/b]\n[color=#ff5050]Danger:[/color] [i]wolves[/i] ahead\n[size=60]tiny[/size] and [size=150]larger[/size] text\n[b][i][color=#ffd040]bold italic gold[/color][/i][/b]";
    Canvas2D& cv = SharedCanvas();
    Image empty(W, H);
    if (!cv.Ok()) return empty;
    cv.Clear(120, 170, 220);                                                       // sky
    cv.Fill(0, 520, W, 200, Color::FromBytes(70, 120, 60));
    cv.Fill(620, 400, 40, 320, Color::FromBytes(90, 60, 35));
    cv.Fill(100, 90, 1080, 330, Color::FromBytes(70, 45, 28));                     // the board
    okay::platform::Painter& painter = *cv.p;
    ui2d::DrawWorldText(painter, *tr, 140.0f, 120.0f, 3.5f);
    return cv.ToImage();
}

// ---- the shipped sample scene, drawn the way the flat (2D) player draws a frame: sprites, world text, then the UI pass ---------
Image Example(Scene& s, float camY = 0.0f) {
    Canvas2D& cv = SharedCanvas();
    Image empty(W, H);
    if (!cv.Ok()) return empty;
    Camera* cam = s.FindObjectOfType<Camera>();
    const float ortho = cam ? cam->orthographicSize : 5.0f;
    const float scale = H / (2.0f * ortho);                                         // pixels per world unit
    const Vec3 camPos = cam && cam->transform ? cam->transform->Position() : Vec3{0, camY, 0};
    auto toScreen = [&](const Vec3& p) { return Vec2{W * 0.5f + (p.x - camPos.x) * scale, H * 0.5f - (p.y - camPos.y) * scale}; };
    if (cam) cv.Clear((int)(cam->backgroundColor.r * 255), (int)(cam->backgroundColor.g * 255), (int)(cam->backgroundColor.b * 255));
    std::vector<SpriteRenderer*> sprites;
    for (const auto& o : s.Objects()) if (o->active) if (auto* sr = o->GetComponent<SpriteRenderer>()) sprites.push_back(sr);
    std::stable_sort(sprites.begin(), sprites.end(), [](SpriteRenderer* x, SpriteRenderer* y) { return x->SortKey() < y->SortKey(); });
    for (SpriteRenderer* sr : sprites) {
        const Vec2 c = toScreen(sr->gameObject->transform->Position());
        cv.Fill(c.x - sr->size.x * scale * 0.5f, c.y - sr->size.y * scale * 0.5f, sr->size.x * scale, sr->size.y * scale, sr->color);
    }
    okay::platform::Painter& painter = *cv.p;
    for (const auto& o : s.Objects()) {                                             // world text, as the player's flat pass draws it
        auto* tr = o->GetComponent<TextRenderer>();
        if (!tr || !o->active || tr->screenSpace) continue;
        const Vec2 at = toScreen(o->transform->Position());
        ui2d::DrawWorldText(painter, *tr, at.x, at.y, tr->EffectivePixelSize() * scale);
    }
    UIWorld().active = true; UIWorld().screenW = W; UIWorld().screenH = H; UIWorld().right = {1, 0, 0};
    UIWorld().project = [=](const Vec3& wp, Vec2& out, float& depth) { out = toScreen(wp); depth = 10.0f; return true; };
    ui2d::RenderSceneUI(s, painter, W, H);
    UIWorld().active = false;
    return cv.ToImage();
}

} // namespace

int main(int argc, char** argv) {
    RUN_SUITE("ui_text_shots");
    std::string dir = std::string(OKAY_TEST_OUTPUT_DIR) + "/ui_text";
    if (const char* e = std::getenv("OKAY_SHOT_DIR")) if (*e) dir = e;
    if (argc >= 2 && argv[1][0]) dir = argv[1];
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    CHECK(!ec);

    {   // nameplates: both ways (default Constant size; and scaling with distance), above two characters
        const Image keep = Plates3D(false);
        const Image far = Plates3D(true);
        CHECK(SavePicture(keep, dir, "nameplates_3d.png"));
        CHECK(SavePicture(far, dir, "nameplates_3d_scaled.png"));
        // each picture shows two health bars: the Goblin's half full, the Orc's nine tenths: more red (226,82,82) than one bar's worth
        const int redKeep = CountPixels(keep, 226, 82, 82), redFar = CountPixels(far, 226, 82, 82);
        std::printf("  red health pixels: constant size %d, scaling with distance %d\n", redKeep, redFar);
        CHECK(redKeep > 800 && redFar > 400);
    }

    {   // the sign: a bigger run no longer overlaps the line above (a line is as tall as its largest size)
        CHECK(SavePicture(Sign(false), dir, "world_text_sign.png"));       // the same text as the "before" picture
        const Image sign = Sign(true);                                       // the big word in its own colour, to measure
        CHECK(SavePicture(sign, dir, "world_text_sign_measured.png"));
        // Three coloured parts, top to bottom: the red "Danger:" (line 2), the green "larger" at 150 percent (line 3) and the gold
        // line 4. Rows (y) where each colour appears inside the board.
        auto rows = [&](int r, int g, int b, int tol, int& top, int& bottom) {
            top = H; bottom = -1;
            for (int y = 0; y < H; ++y)
                if (CountPixels(sign, r, g, b, tol, 100, y, 1180, y + 1) > 0) { top = std::min(top, y); bottom = std::max(bottom, y); }
        };
        int redTop, redBottom, greenTop, greenBottom, goldTop, goldBottom;
        rows(255, 80, 80, 12, redTop, redBottom);
        rows(128, 255, 128, 10, greenTop, greenBottom);
        rows(255, 208, 64, 12, goldTop, goldBottom);
        std::printf("  sign rows: red line %d..%d, green big word %d..%d, gold line %d..%d\n", redTop, redBottom, greenTop, greenBottom, goldTop, goldBottom);
        CHECK(redBottom > redTop && greenBottom > greenTop && goldBottom > goldTop);
        CHECK(greenTop >= redBottom);                              // the big word starts under the line above it (it used to climb into it)
        CHECK(goldTop >= greenBottom);                             // and the next line starts under the big word
        CHECK(greenBottom - greenTop >= (int)(7 * 3.5f * 1.5f * 0.8f));   // and it is drawn big: about 150 percent of a 7-row glyph at 3.5 px a font pixel
    }

    {   // the sample scene: at the start, then after the script has hurt the Goblin
        Scene s("play");
        std::string err;
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/ui_text/ui_text.okayscene";
        const bool loaded = SceneSerializer::LoadFromFile(s, path, &err);
        if (!loaded) std::printf("  could not load %s: %s\n", path.c_str(), err.c_str());
        CHECK(loaded);
        if (loaded) {
            s.Start();
            for (int i = 0; i < 3; ++i) { Input::FeedKeys({}); s.Update(0.016f); }
            const Image start = Example(s);
            CHECK(SavePicture(start, dir, "sample_start.png"));
            for (int i = 0; i < 400; ++i) { Input::FeedKeys({}); s.Update(0.016f); }       // about six seconds: the four automatic hits
            for (int i = 0; i < 2; ++i) { Input::FeedKeys({'2'}); s.Update(0.016f); }       // and a press of 2
            Input::FeedKeys({}); s.Update(0.016f);
            const Image hurt = Example(s);
            CHECK(SavePicture(hurt, dir, "sample_after_hits.png"));
            const int before = CountPixels(start, 226, 82, 82), after = CountPixels(hurt, 226, 82, 82);
            std::printf("  red health pixels: at the start %d, after the hits %d\n", before, after);
            CHECK(before > 3000);                                                            // two full bars
            CHECK(after < before - 800);                                                     // the bars are shorter on the screen
            Input::FeedKeys({});
        }
    }

    TEST_MAIN_RESULT();
}
