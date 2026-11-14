// OkayUI render backend: software. The reference every other backend is compared with. See OkayUIRenderBackends.hpp.
#include "okay/UI/OkayUIRenderBackends.hpp"
#include "OkayUI_Soft.hpp"
#include "OkayUI_SoftRaster.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace OkayUI {
namespace render {
namespace {

class SoftBackend final : public Backend {
public:
    explicit SoftBackend(soft::Bitmap& t) : m_t(t) {}
    const char* Name() const override { return "software"; }
    Caps Capabilities() const override { Caps c; c.maxTexture = kMaxTexture; return c; }

    TexId CreateTexture(int w, int h, bool linear, const std::uint8_t* rgba) override {
        if (w <= 0 || h <= 0 || w > kMaxTexture || h > kMaxTexture) return 0;
        Tex t; t.w = w; t.h = h; t.linear = linear;
        t.px.assign((size_t)w * (size_t)h * 4, 0);
        if (rgba) std::memcpy(t.px.data(), rgba, t.px.size());
        const TexId id = ++m_next;
        m_tex[id] = std::move(t);
        return id;
    }
    bool UpdateTexture(TexId id, int w, int h, const std::uint8_t* rgba) override {
        auto it = m_tex.find(id);
        if (it == m_tex.end() || w <= 0 || h <= 0 || w > kMaxTexture || h > kMaxTexture || !rgba) return false;
        it->second.w = w; it->second.h = h;
        it->second.px.assign(rgba, rgba + (size_t)w * (size_t)h * 4);
        return true;
    }
    void DestroyTexture(TexId id) override { m_tex.erase(id); }

    bool BeginFrame(int fbW, int fbH, float dpi) override {
        if (m_t.w != fbW || m_t.h != fbH || m_t.rgba.size() != (size_t)fbW * (size_t)fbH * 4) return false;
        m_dpi = dpi;
        return true;
    }
    void Draw(const Vertex* vtx, int nv, const std::int32_t* idx, int ni, const Span* spans, int ns) override {
        const Vertex* v = vtx;
        if (m_dpi != 1.0f) {            // logical -> framebuffer pixels
            m_scaled.assign(vtx, vtx + nv);
            for (Vertex& s : m_scaled) { s.x *= m_dpi; s.y *= m_dpi; }
            v = m_scaled.data();
        }
        soft::raster::Surface surf; surf.px = m_t.rgba.data(); surf.w = m_t.w; surf.h = m_t.h;
        for (int s = 0; s < ns; ++s) {
            const Span& sp = spans[s];
            soft::raster::Clip clip{ 0, 0, m_t.w, m_t.h };
            if (sp.clipOn) clip = soft::raster::Clip{ sp.x0, sp.y0, sp.x1, sp.y1 };
            soft::raster::Tex tex; const soft::raster::Tex* tp = nullptr;
            if (sp.tex) {
                auto it = m_tex.find(sp.tex);
                if (it == m_tex.end()) continue;
                tex.w = it->second.w; tex.h = it->second.h; tex.rgba = it->second.px.data(); tex.linear = it->second.linear;
                tp = &tex;
            }
            const std::int64_t end = (std::int64_t)sp.idxOffset + sp.idxCount;
            for (std::int64_t i = sp.idxOffset; i + 3 <= end && i + 3 <= ni; i += 3)
                soft::raster::Triangle(surf, clip, tp, true, v[idx[i]], v[idx[i + 1]], v[idx[i + 2]]);
        }
    }
    void EndFrame() override {}
    bool ReadPixels(std::vector<std::uint8_t>& out, int& w, int& h) override { out = m_t.rgba; w = m_t.w; h = m_t.h; return true; }

private:
    struct Tex { int w = 0, h = 0; bool linear = false; std::vector<std::uint8_t> px; };
    soft::Bitmap& m_t;
    float m_dpi = 1.0f;
    std::unordered_map<TexId, Tex> m_tex;
    TexId m_next = 0;
    std::vector<Vertex> m_scaled;
};

} // namespace

std::unique_ptr<Backend> CreateSoftBackend(soft::Bitmap& target) { return std::unique_ptr<Backend>(new SoftBackend(target)); }

} // namespace render
} // namespace OkayUI
