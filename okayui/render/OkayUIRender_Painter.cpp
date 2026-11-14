// OkayUI render backend: okay::platform::Painter (sdl2 painter or the own software painter). See OkayUIRenderBackends.hpp.
#include "okay/UI/OkayUIRenderBackends.hpp"
#include "okay/Platform/Painter.hpp"

#include <unordered_map>

namespace OkayUI {
namespace render {
namespace {

namespace plat = okay::platform;
namespace ui2d = okay::ui2d;

class PainterBackend final : public Backend {
public:
    explicit PainterBackend(plat::Painter& p) : m_p(p) {}
    ~PainterBackend() override { for (auto& kv : m_tex) m_p.DestroyTexture(kv.second.tex); }
    const char* Name() const override { return m_p.Kind() == plat::PainterKind::Sdl2 ? "painter-sdl2" : "painter-software"; }
    Caps Capabilities() const override { Caps c; c.maxTexture = 16384; return c; }

    TexId CreateTexture(int w, int h, bool linear, const std::uint8_t* rgba) override {
        plat::TextureDesc d; d.width = w; d.height = h; d.blend = plat::BlendMode::Blend;
        d.scale = linear ? plat::ScaleMode::Linear : plat::ScaleMode::Nearest;
        ui2d::Tex t = m_p.CreateTexture(d, rgba);
        if (!t) return 0;
        const TexId id = ++m_next;
        m_tex[id] = Entry{ t, w, h, linear };
        return id;
    }
    bool UpdateTexture(TexId id, int w, int h, const std::uint8_t* rgba) override {
        auto it = m_tex.find(id);
        if (it == m_tex.end() || !rgba) return false;
        Entry& e = it->second;
        if (e.w != w || e.h != h) {      // the painter cannot resize a texture: replace it under the same id
            plat::TextureDesc d; d.width = w; d.height = h; d.blend = plat::BlendMode::Blend;
            d.scale = e.linear ? plat::ScaleMode::Linear : plat::ScaleMode::Nearest;
            ui2d::Tex t = m_p.CreateTexture(d, rgba);
            if (!t) return false;
            m_p.DestroyTexture(e.tex);
            e.tex = t; e.w = w; e.h = h;
            return true;
        }
        return m_p.UpdateTexture(e.tex, nullptr, rgba, w * 4);
    }
    void DestroyTexture(TexId id) override {
        auto it = m_tex.find(id);
        if (it == m_tex.end()) return;
        m_p.DestroyTexture(it->second.tex);
        m_tex.erase(it);
    }

    bool BeginFrame(int fbW, int fbH, float dpi) override {
        int w = 0, h = 0;
        m_p.OutputSize(&w, &h);
        if (w != fbW || h != fbH) return false;       // the host sized the frame for another target
        m_dpi = dpi;
        m_savedBlend = m_p.GetBlendMode();
        m_hadClip = m_p.IsClipEnabled();
        m_savedClip = ui2d::Rect{0, 0, 0, 0};
        m_p.GetClipRect(&m_savedClip);
        m_p.SetBlendMode(plat::BlendMode::Blend);
        return true;
    }
    void Draw(const Vertex* vtx, int nv, const std::int32_t* idx, int ni, const Span* spans, int ns) override {
        static_assert(sizeof(Vertex) == sizeof(ui2d::Vertex), "DrawVertex must match ui2d::Vertex");
        const ui2d::Vertex* v = (const ui2d::Vertex*)vtx;
        if (m_dpi != 1.0f) {
            m_scaled.assign(vtx, vtx + nv);
            for (Vertex& s : m_scaled) { s.x *= m_dpi; s.y *= m_dpi; }
            v = (const ui2d::Vertex*)m_scaled.data();
        }
        for (int s = 0; s < ns; ++s) {
            const Span& sp = spans[s];
            ui2d::Tex tex = nullptr;
            if (sp.tex) {
                auto it = m_tex.find(sp.tex);
                if (it == m_tex.end()) continue;
                tex = it->second.tex;
            }
            if (sp.clipOn) { const ui2d::Rect r{ sp.x0, sp.y0, sp.x1 - sp.x0, sp.y1 - sp.y0 }; m_p.SetClipRect(&r); }
            else m_p.SetClipRect(nullptr);
            if (sp.idxOffset + sp.idxCount > ni) continue;
            m_p.Geometry(tex, v, nv, (const int*)idx + sp.idxOffset, sp.idxCount);
        }
    }
    void EndFrame() override {
        m_p.SetClipRect(m_hadClip ? &m_savedClip : nullptr);
        m_p.SetBlendMode(m_savedBlend);
    }
    bool ReadPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) override { return m_p.ReadPixels(rgba, w, h); }

private:
    struct Entry { ui2d::Tex tex = nullptr; int w = 0, h = 0; bool linear = false; };
    plat::Painter& m_p;
    float m_dpi = 1.0f;
    plat::BlendMode m_savedBlend = plat::BlendMode::Blend;
    bool m_hadClip = false;
    ui2d::Rect m_savedClip{0, 0, 0, 0};
    std::unordered_map<TexId, Entry> m_tex;
    TexId m_next = 0;
    std::vector<Vertex> m_scaled;
};

} // namespace

std::unique_ptr<Backend> CreatePainterBackend(okay::platform::Painter& painter) { return std::unique_ptr<Backend>(new PainterBackend(painter)); }

} // namespace render
} // namespace OkayUI
