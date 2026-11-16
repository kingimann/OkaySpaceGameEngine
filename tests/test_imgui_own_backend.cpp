// Our own Dear ImGui backend (shared/ImGuiOwnBackend.*) with the real Dear ImGui and no window, no SDL, no GPU:
// platform events go in through okay::imguibackend::OwnBackend::ProcessEvent, a frame is built, and the draw data
// goes out through okay::platform::Painter into a small test painter that rasterises with OkayUI's triangle routine.
// Checks: a button reacts to a click (move, press, release), typed text and Backspace edit an input field, Ctrl is
// seen, the wheel arrives, the frame has pixels (the button face and glyphs from the converted font texture), the
// font texture is created and destroyed with the backend.
#include "test_framework.hpp"
#include "ImGuiOwnBackend.hpp"
#include "imgui.h"
#include "OkayUI_SoftRaster.hpp"

#include <cstring>
#include <set>

namespace plat = okay::platform;
namespace ui2d = okay::ui2d;
namespace raster = OkayUI::soft::raster;

// The base-class helpers normally live in the engine (engine/src/Platform/Painter.cpp); this test does not link the engine.
namespace okay::platform {
ui2d::Tex Painter::LoadTexture(const std::string&) { return nullptr; }
ui2d::Tex Painter::FontAtlas(okay::TtfFont*) { return nullptr; }
ui2d::Tex Painter::UploadAtlas(const okay::Image&, int, ui2d::Tex) { return nullptr; }
ui2d::Tex Painter::CachedTexture(const std::string&) const { return nullptr; }
void Painter::ReleaseCachedTextures() {}
}

namespace {
struct TestTex { int w = 0, h = 0; std::vector<std::uint8_t> px; bool linear = false; };

class TestPainter final : public plat::Painter {
public:
    TestPainter(int w, int h) : m_w(w), m_h(h), m_px((size_t)w * h * 4, 0) {}
    ~TestPainter() override { for (TestTex* t : m_tex) delete t; }
    plat::PainterKind Kind() const override { return plat::PainterKind::Software; }
    void* NativeRenderer() override { return nullptr; }
    void OutputSize(int* w, int* h) override { if (w) *w = m_w; if (h) *h = m_h; }
    void Clear() override { for (size_t i = 0; i < m_px.size(); i += 4) { m_px[i] = m_r; m_px[i + 1] = m_g; m_px[i + 2] = m_b; m_px[i + 3] = 255; } }
    void SetBlendMode(plat::BlendMode m) override { m_blend = m; }
    plat::BlendMode GetBlendMode() override { return m_blend; }
    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t) override { m_r = r; m_g = g; m_b = b; }
    void FillRect(const ui2d::Rect*) override {}
    void FillRectF(const ui2d::FRect*) override {}
    void DrawRect(const ui2d::Rect*) override {}
    void DrawLine(int, int, int, int) override {}
    void DrawLineF(float, float, float, float) override {}
    void DrawPoint(int, int) override {}
    void SetClipRect(const ui2d::Rect* r) override { m_clipOn = r != nullptr; if (r) m_clip = *r; }
    void GetClipRect(ui2d::Rect* r) override { if (r) *r = m_clipOn ? m_clip : ui2d::Rect{0, 0, 0, 0}; }
    bool IsClipEnabled() override { return m_clipOn; }
    void SetScale(float sx, float sy) override { m_sx = sx; m_sy = sy; }
    void QueryTexture(ui2d::Tex t, int* w, int* h) override { auto* x = static_cast<TestTex*>(t); if (w) *w = x ? x->w : 0; if (h) *h = x ? x->h : 0; }
    void SetTextureColorMod(ui2d::Tex, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void SetTextureAlphaMod(ui2d::Tex, std::uint8_t) override {}
    void Copy(ui2d::Tex, const ui2d::Rect*, const ui2d::Rect*) override {}
    void CopyF(ui2d::Tex, const ui2d::Rect*, const ui2d::FRect*) override {}
    void CopyEx(ui2d::Tex, const ui2d::Rect*, const ui2d::Rect*, double, const ui2d::Point*, int) override {}
    void CopyExF(ui2d::Tex, const ui2d::Rect*, const ui2d::FRect*, double, const ui2d::FPoint*, int) override {}
    ui2d::Tex CreateTexture(const plat::TextureDesc& d, const std::uint8_t* rgba) override {
        auto* t = new TestTex; t->w = d.width; t->h = d.height; t->linear = d.scale == plat::ScaleMode::Linear;
        t->px.assign((size_t)d.width * d.height * 4, 0);
        if (rgba) std::memcpy(t->px.data(), rgba, t->px.size());
        m_tex.insert(t);
        return t;
    }
    bool UpdateTexture(ui2d::Tex tex, const ui2d::Rect* r, const void* rgba, int pitch) override {
        auto* t = static_cast<TestTex*>(tex);
        if (!m_tex.count(t) || !rgba) return false;
        ui2d::Rect rc{0, 0, t->w, t->h}; if (r) rc = *r;
        for (int y = 0; y < rc.h; ++y) std::memcpy(&t->px[((size_t)(rc.y + y) * t->w + rc.x) * 4], (const std::uint8_t*)rgba + (size_t)y * pitch, (size_t)rc.w * 4);
        ++updates;
        return true;
    }
    void DestroyTexture(ui2d::Tex tex) override { auto* t = static_cast<TestTex*>(tex); if (m_tex.erase(t)) delete t; }
    void SetTextureBlendMode(ui2d::Tex, plat::BlendMode) override {}
    void SetTextureScaleMode(ui2d::Tex, plat::ScaleMode) override {}
    bool SetTarget(ui2d::Tex) override { return false; }
    ui2d::Tex GetTarget() override { return nullptr; }
    void Present() override {}
    bool SetVSync(bool) override { return false; }
    bool ReadPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) override { rgba = m_px; w = m_w; h = m_h; return true; }
    void Geometry(ui2d::Tex tex, const ui2d::Vertex* v, int nv, const int* idx, int ni) override {
        ++geometryCalls;
        auto* t = static_cast<TestTex*>(tex);
        raster::Tex view;
        if (t) { view.w = t->w; view.h = t->h; view.rgba = t->px.data(); view.linear = t->linear; }
        raster::Surface s{m_px.data(), m_w, m_h};
        raster::Clip clip{0, 0, m_w, m_h};
        if (m_clipOn) { clip.x0 = (long long)(m_clip.x * m_sx); clip.y0 = (long long)(m_clip.y * m_sy); clip.x1 = (long long)((m_clip.x + m_clip.w) * m_sx); clip.y1 = (long long)((m_clip.y + m_clip.h) * m_sy); }
        auto V = [&](int i) { OkayUI::DrawVertex d; d.x = v[i].position.x * m_sx; d.y = v[i].position.y * m_sy; d.r = v[i].color.r; d.g = v[i].color.g; d.b = v[i].color.b; d.a = v[i].color.a; d.u = v[i].tex_coord.x; d.v = v[i].tex_coord.y; return d; };
        for (int i = 0; i + 3 <= ni; i += 3) {
            if (idx[i] >= nv || idx[i + 1] >= nv || idx[i + 2] >= nv) { badIndex = true; continue; }
            raster::Triangle(s, clip, t ? &view : nullptr, true, V(idx[i]), V(idx[i + 1]), V(idx[i + 2]));
        }
    }
    size_t TextureCount() const { return m_tex.size(); }
    const std::uint8_t* At(int x, int y) const { return &m_px[((size_t)y * m_w + x) * 4]; }
    int geometryCalls = 0, updates = 0;
    bool badIndex = false;
private:
    int m_w, m_h;
    std::vector<std::uint8_t> m_px;
    std::set<TestTex*> m_tex;
    std::uint8_t m_r = 20, m_g = 22, m_b = 30;
    plat::BlendMode m_blend = plat::BlendMode::None;
    bool m_clipOn = false;
    ui2d::Rect m_clip{0, 0, 0, 0};
    float m_sx = 1, m_sy = 1;
};

plat::Event Ev(plat::EventType t) { plat::Event e; e.type = t; return e; }

struct Frame {
    okay::imguibackend::OwnBackend& be;
    TestPainter& p;
    template <class F> void Run(F f) {
        be.NewFrame();
        ImGui::NewFrame();
        f();
        ImGui::Render();
        p.Clear();
        be.RenderDrawData(ImGui::GetDrawData());
    }
};
} // namespace

int main() {
    RUN_SUITE("own Dear ImGui backend, headless");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    TestPainter painter(320, 200);
    {
        okay::imguibackend::OwnBackend be;
        CHECK(be.Init(nullptr, nullptr, &painter));
        CHECK(!be.Init(nullptr, nullptr, &painter));        // not twice
        Frame fr{be, painter};

        bool clicked = false;
        char text[32] = "";
        bool keyUpSeen = false, ctrlSeen = false;
        float wheel = 0;
        int frameNo = 0;
        auto ui = [&] {
            ImGui::SetNextWindowPos(ImVec2(10, 10));
            ImGui::SetNextWindowSize(ImVec2(300, 160));
            ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
            if (frameNo == 0) ImGui::SetKeyboardFocusHere();
            ImGui::InputText("##in", text, sizeof(text));
            if (ImGui::Button("Hello", ImVec2(100, 30))) clicked = true;
            keyUpSeen = keyUpSeen || ImGui::IsKeyReleased(ImGuiKey_Backspace);
            ctrlSeen = ctrlSeen || io.KeyCtrl;
            wheel += io.MouseWheel;
            ImGui::End();
            ++frameNo;
        };

        fr.Run(ui);                                          // frame 0: input field gets the focus
        fr.Run(ui);
        CHECK(painter.TextureCount() == 1);                  // the font texture was created through the painter
        CHECK(painter.geometryCalls > 0 && !painter.badIndex);
        // The window background is drawn: a pixel inside it differs from the clear colour.
        const std::uint8_t* inside = painter.At(200, 150);
        CHECK(!(inside[0] == 20 && inside[1] == 22 && inside[2] == 30));
        const std::uint8_t* outside = painter.At(2, 2);
        CHECK(outside[0] == 20 && outside[1] == 22 && outside[2] == 30);   // outside the window: untouched

        // Text: typed characters and Backspace edit the focused input field.
        okay::imguibackend::OwnBackend* pb = &be;
        io.AddKeyEvent(ImGuiKey_None, false);                // harmless
        plat::Event tx = Ev(plat::EventType::TextInput); tx.text = "ab\xC3\xA4";
        pb->ProcessEvent(tx);
        fr.Run(ui);
        CHECK(std::string(text) == "ab\xC3\xA4");
        plat::Event bk = Ev(plat::EventType::KeyDown); bk.key = plat::Key::Backspace;
        pb->ProcessEvent(bk);
        fr.Run(ui);
        bk.type = plat::EventType::KeyUp;
        pb->ProcessEvent(bk);
        fr.Run(ui);
        CHECK(std::string(text) == "ab");
        CHECK(keyUpSeen);

        // Ctrl and the wheel.
        plat::Event ct = Ev(plat::EventType::KeyDown); ct.key = plat::Key::LCtrl; ct.mods = plat::ModCtrl;
        pb->ProcessEvent(ct);
        plat::Event wh = Ev(plat::EventType::MouseWheel); wh.wheelY = 2.0f;
        pb->ProcessEvent(wh);
        fr.Run(ui);
        CHECK(ctrlSeen);
        CHECK(wheel > 0.0f);
        ct.type = plat::EventType::KeyUp; ct.mods = 0;
        pb->ProcessEvent(ct);

        // Click the button: move onto it, press, release (each in its own frame, as a real mouse does).
        // The button sits at window (10,10) + padding, below the input field: find it from ImGui.
        ImVec2 center(0, 0);
        auto find = [&] {
            ImGui::SetNextWindowPos(ImVec2(10, 10)); ImGui::SetNextWindowSize(ImVec2(300, 160));
            ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
            ImGui::InputText("##in", text, sizeof(text));
            ImGui::Button("Hello", ImVec2(100, 30));
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            center = ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            ImGui::End();
        };
        fr.Run(find);
        CHECK(center.x > 10 && center.y > 20);
        plat::Event mv = Ev(plat::EventType::MouseMove); mv.x = (int)center.x; mv.y = (int)center.y;
        pb->ProcessEvent(mv);
        fr.Run(ui);
        CHECK(!clicked);
        plat::Event down = Ev(plat::EventType::MouseButtonDown); down.button = plat::MouseLeft; down.x = mv.x; down.y = mv.y; down.clicks = 1;
        pb->ProcessEvent(down);
        fr.Run(ui);
        plat::Event up = down; up.type = plat::EventType::MouseButtonUp;
        pb->ProcessEvent(up);
        fr.Run(ui);
        CHECK(clicked);
        // The hovered/active button face is drawn: some pixel of the button area is not the window background.
        bool faceDiffers = false;
        const std::uint8_t* bg = painter.At(250, 150);
        for (int x = (int)center.x - 40; x < (int)center.x + 40; ++x) {
            const std::uint8_t* q = painter.At(x, (int)center.y);
            if (q[0] != bg[0] || q[1] != bg[1] || q[2] != bg[2]) faceDiffers = true;
        }
        CHECK(faceDiffers);
        // Glyphs: the font texture contributes pixels of a third colour inside the button (white-ish text on the face).
        int bright = 0;
        for (int y = (int)center.y - 8; y < (int)center.y + 8; ++y)
            for (int x = (int)center.x - 25; x < (int)center.x + 25; ++x) { const std::uint8_t* q = painter.At(x, y); if (q[0] > 200 && q[1] > 200 && q[2] > 200) ++bright; }
        CHECK(bright > 10);

        // Losing the focus clears input state.
        pb->ProcessEvent(Ev(plat::EventType::WindowFocusLost));
        fr.Run(ui);
        CHECK(!io.MouseDown[0]);
        be.Shutdown();
        CHECK(painter.TextureCount() == 0);                  // the font texture is gone with the backend
    }
    ImGui::DestroyContext();
    TEST_MAIN_RESULT();
}
