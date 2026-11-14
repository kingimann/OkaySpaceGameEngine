// OkayUI render interface: the validator and the front end every backend sits behind. See OkayUIRender.hpp.
#include "okay/UI/OkayUIRender.hpp"
#include "okay/Graphics/TtfFont.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace OkayUI {
namespace render {

const char* ProblemName(Problem p) {
    switch (p) {
        case Problem::None: return "none";
        case Problem::NullPointer: return "null pointer";
        case Problem::BadStride: return "bad vertex stride";
        case Problem::NegativeCount: return "negative count";
        case Problem::TooMany: return "count above the limit";
        case Problem::BadTarget: return "bad target size or DPI";
        case Problem::IndexCountNotTriangles: return "index count not a multiple of 3";
        case Problem::IndexOutOfRange: return "index out of range";
        case Problem::BadVertex: return "non-finite or huge vertex";
        case Problem::SpanOutOfRange: return "span outside the index buffer";
        case Problem::SpanNotTriangles: return "span count not a multiple of 3";
        case Problem::BadClip: return "bad clip rectangle";
        case Problem::SpansNotAscending: return "spans not ascending";
        case Problem::UncoveredIndices: return "indices no span draws";
        case Problem::UnresolvedTexture: return "unresolved texture key";
        case Problem::Dropped: return "OkayUI dropped geometry";
    }
    return "?";
}

namespace {

inline bool GoodPos(float v) { return std::isfinite(v) && std::fabs(v) <= kMaxCoord; }

void Note(Report& r, Problem p, const char* fmt, long long a = 0, long long b = 0) {
    if (r.first != Problem::None) return;
    r.first = p;
    char buf[160];
    std::snprintf(buf, sizeof buf, fmt, a, b);
    r.message = std::string(ProblemName(p)) + ": " + buf;
}

Report Fatal(Problem p, const char* what, long long a = 0, long long b = 0) {
    Report r;
    r.fatal = 1;
    Note(r, p, what, a, b);
    return r;
}

// A span's index range after clamping, in 64-bit maths so INT_MAX offsets cannot wrap.
struct Range { std::int64_t from = 0, to = 0; bool dropped = false; };
Range ClampRange(const DrawCmd& c, int indexCount) {
    Range r;
    if (c.idxOffset < 0 || c.idxCount < 0 || (std::int64_t)c.idxOffset > indexCount) { r.dropped = true; return r; }
    r.from = c.idxOffset;
    r.to = std::min<std::int64_t>(indexCount, (std::int64_t)c.idxOffset + c.idxCount);
    r.to = r.from + (r.to - r.from) / 3 * 3;
    return r;
}

bool SpanWholeOk(const DrawCmd& c, int indexCount) {
    return c.idxOffset >= 0 && c.idxCount >= 0 && (std::int64_t)c.idxOffset + c.idxCount <= indexCount;
}

} // namespace

Report Validate(const DrawData& d, int fbW, int fbH, float dpi, const ValidateOptions& o) {
    // ---- fatal: the shape of the data itself -------------------------------------------------------------
    if (fbW <= 0 || fbH <= 0 || fbW > kMaxTarget || fbH > kMaxTarget || !std::isfinite(dpi) || dpi < kMinDpi || dpi > kMaxDpi)
        return Fatal(Problem::BadTarget, "target %lld x %lld (or DPI) outside the supported range", fbW, fbH);
    if (d.vertexCount < 0 || d.indexCount < 0 || d.cmdCount < 0) return Fatal(Problem::NegativeCount, "vertices %lld, indices %lld", d.vertexCount, d.indexCount);
    if (d.vertexCount > kMaxVertices || d.indexCount > kMaxIndices || d.cmdCount > kMaxSpans) return Fatal(Problem::TooMany, "vertices %lld, indices %lld", d.vertexCount, d.indexCount);
    if (d.vertexCount > 0 && !d.vertices) return Fatal(Problem::NullPointer, "vertices null with count %lld", d.vertexCount);
    if (d.indexCount > 0 && !d.indices) return Fatal(Problem::NullPointer, "indices null with count %lld", d.indexCount);
    if (d.cmdCount > 0 && !d.cmds) return Fatal(Problem::NullPointer, "commands null with count %lld", d.cmdCount);
    if (d.vertexCount > 0 && d.vertexStride != (int)sizeof(Vertex)) return Fatal(Problem::BadStride, "stride %lld, expected %lld", d.vertexStride, (long long)sizeof(Vertex));
    if (d.atlasTexture && d.atlasRgba && (d.atlasW <= 0 || d.atlasH <= 0 || d.atlasW > kMaxTexture || d.atlasH > kMaxTexture))
        return Fatal(Problem::BadTarget, "atlas size %lld x %lld", d.atlasW, d.atlasH);

    Report r;
    // ---- repairable ----------------------------------------------------------------------------------------
    if (d.indexCount % 3 != 0) { r.trailingIndices = true; Note(r, Problem::IndexCountNotTriangles, "indexCount %lld", d.indexCount); }
    if (o.checkVertices && d.vertexCount > 0) {
        const Vertex* v = (const Vertex*)d.vertices;
        for (int i = 0; i < d.vertexCount; ++i) {
            if (!GoodPos(v[i].x) || !GoodPos(v[i].y)) { ++r.badVertices; Note(r, Problem::BadVertex, "vertex %lld", i); }
            else if (!std::isfinite(v[i].u) || !std::isfinite(v[i].v)) { ++r.nanUv; Note(r, Problem::BadVertex, "vertex %lld uv", i); }
        }
    }
    if (o.checkIndices && d.indexCount > 0) {
        const unsigned n = (unsigned)d.vertexCount;
        for (int i = 0; i < d.indexCount; ++i)
            if ((unsigned)d.indices[i] >= n) { ++r.badIndices; Note(r, Problem::IndexOutOfRange, "index #%lld is %lld", i, d.indices[i]); }
    }
    std::int64_t covered = 0, prevEnd = 0;
    for (int s = 0; s < d.cmdCount; ++s) {
        const DrawCmd& c = d.cmds[s];
        if (!SpanWholeOk(c, d.indexCount)) { ++r.badSpans; Note(r, Problem::SpanOutOfRange, "span %lld: offset %lld", s, c.idxOffset); }
        else if (c.idxCount % 3 != 0) { ++r.badSpans; Note(r, Problem::SpanNotTriangles, "span %lld: count %lld", s, c.idxCount); }
        if (c.clipOn) {
            const bool neg = c.clipW < 0 || c.clipH < 0;
            const bool overflow = (std::int64_t)c.clipX + c.clipW > 2147483647LL || (std::int64_t)c.clipY + c.clipH > 2147483647LL;
            if (neg || overflow) { ++r.badClips; Note(r, Problem::BadClip, "span %lld clip", s); }
        }
        const Range rg = ClampRange(c, d.indexCount);
        if (!rg.dropped) {
            if (rg.from < prevEnd && rg.to > rg.from) ++r.spansOutOfOrder;
            if (rg.to > rg.from) prevEnd = std::max(prevEnd, rg.to);
            covered += rg.to - rg.from;
        }
        if (c.texture && o.resolvable && !o.resolvable(c.texture, o.resolvableUser)) ++r.unresolvedSpans;
    }
    // ---- contract warnings (legal, but OkayUI never does this) ----------------------------------------------
    if (d.cmdCount > 0 && covered < d.indexCount) r.uncoveredIndices = (int)std::min<std::int64_t>(d.indexCount - covered, 2147483647);
    if (r.spansOutOfOrder) Note(r, Problem::SpansNotAscending, "%lld span(s) go backwards or overlap", r.spansOutOfOrder);
    if (r.uncoveredIndices) Note(r, Problem::UncoveredIndices, "%lld index(es) not drawn", r.uncoveredIndices);
    if (r.unresolvedSpans) Note(r, Problem::UnresolvedTexture, "%lld span(s)", r.unresolvedSpans);
    r.droppedByOkayUI = d.dropped;
    if (d.dropped > 0) Note(r, Problem::Dropped, "%lld shape(s)", d.dropped);
    return r;
}

// ---- the front end -----------------------------------------------------------------------------------------

Renderer::~Renderer() { ReleaseAll(); }

void Renderer::ReleaseAll() {
    for (auto& kv : m_images) if (kv.second.id) m_b.DestroyTexture(kv.second.id);
    m_images.clear();
    for (auto& kv : m_fonts) if (kv.second.id) m_b.DestroyTexture(kv.second.id);
    m_fonts.clear();
    if (m_atlas) m_b.DestroyTexture(m_atlas);
    m_atlas = 0; m_atlasKey = nullptr; m_atlasW = m_atlasH = 0;
}

bool Renderer::SetImage(const void* key, int w, int h, const std::uint8_t* rgba, bool linear) {
    if (!key || w <= 0 || h <= 0 || w > kMaxTexture || h > kMaxTexture) return false;
    auto it = m_images.find(key);
    if (it != m_images.end()) { m_b.DestroyTexture(it->second.id); m_images.erase(it); }
    const TexId id = m_b.CreateTexture(w, h, linear, rgba);
    if (!id) return false;
    m_images[key] = Img{ id, w, h };
    return true;
}

void Renderer::RemoveImage(const void* key) {
    auto it = m_images.find(key);
    if (it == m_images.end()) return;
    m_b.DestroyTexture(it->second.id);
    m_images.erase(it);
}

Stats Renderer::Render(const DrawData& d, const FrameInfo& f) {
    Stats st;
    ValidateOptions vo;
    vo.checkIndices = m_o.checkIndices; vo.checkVertices = m_o.checkVertices;
    const Report rep = Validate(d, f.fbWidth, f.fbHeight, f.dpi, vo);
    st.firstProblem = rep.first; st.message = rep.message;
    if (rep.refused() || (m_o.strict && !rep.ok())) { st.refused = true; return st; }
    if (d.vertexCount <= 0 || d.indexCount < 3 || d.cmdCount <= 0) return st;     // nothing to draw

    // ---- textures ------------------------------------------------------------------------------------------
    // The atlas: uploaded when first seen or when its key or size changed.
    if (d.atlasTexture && d.atlasRgba && d.atlasW > 0 && d.atlasH > 0) {
        if (m_atlas && (m_atlasKey != d.atlasTexture || m_atlasW != d.atlasW || m_atlasH != d.atlasH)) { m_b.DestroyTexture(m_atlas); m_atlas = 0; }
        if (!m_atlas) {
            m_atlas = m_b.CreateTexture(d.atlasW, d.atlasH, m_o.atlasLinear, d.atlasRgba);
            m_atlasKey = d.atlasTexture; m_atlasW = d.atlasW; m_atlasH = d.atlasH;
            if (m_atlas) ++st.texturesUploaded;
        }
    }
    auto resolve = [&](const void* key) -> TexId {
        if (!key) return 0;
        if (key == d.atlasTexture) return m_atlas;
        auto im = m_images.find(key);
        if (im != m_images.end()) return im->second.id;
        if (okay::TtfFont* font = FallbackFontOf(key)) {
            if (!font->Valid()) return 0;
            const okay::Image& a = font->Atlas();
            if (a.Width() <= 0 || a.Height() <= 0 || a.Width() > kMaxTexture || a.Height() > kMaxTexture) return 0;
            FontTex& ft = m_fonts[key];
            if (ft.id && (ft.w != a.Width() || ft.h != a.Height() || ft.gen != font->Generation())) {
                if (ft.w != a.Width() || ft.h != a.Height()) { m_b.DestroyTexture(ft.id); ft.id = 0; }
                else if (m_b.UpdateTexture(ft.id, a.Width(), a.Height(), a.Data())) { ft.gen = font->Generation(); ++st.texturesUploaded; }
            }
            if (!ft.id) {
                ft.id = m_b.CreateTexture(a.Width(), a.Height(), true, a.Data());
                ft.w = a.Width(); ft.h = a.Height(); ft.gen = font->Generation();
                if (ft.id) ++st.texturesUploaded;
            }
            return ft.id;
        }
        return 0;
    };

    // ---- spans (and, when something is broken, repaired copies of the arrays) -------------------------------
    const bool repair = rep.needsRepair();
    const Vertex* vtx = (const Vertex*)d.vertices;
    const std::int32_t* idx = d.indices;
    int idxCount = d.indexCount;
    std::vector<std::uint8_t> badV;
    if (repair) {
        m_vtx.assign(vtx, vtx + d.vertexCount);
        badV.assign((size_t)d.vertexCount, 0);
        for (int i = 0; i < d.vertexCount; ++i) {
            Vertex& v = m_vtx[(size_t)i];
            if (!GoodPos(v.x) || !GoodPos(v.y)) badV[(size_t)i] = 1;
            if (!std::isfinite(v.u)) v.u = 0.0f;
            if (!std::isfinite(v.v)) v.v = 0.0f;
        }
        vtx = m_vtx.data();
        m_idx.clear();
        m_idx.reserve((size_t)d.indexCount);
        st.repaired = true;
    }
    m_spans.clear();
    m_spans.reserve((size_t)d.cmdCount);
    const double dpi = f.dpi;
    for (int s = 0; s < d.cmdCount; ++s) {
        const DrawCmd& c = d.cmds[s];
        Range rg = ClampRange(c, d.indexCount);
        if (rg.dropped || rg.to <= rg.from) { if (c.idxCount > 0) ++st.spansSkipped; continue; }
        if (c.clipOn && (c.clipW < 0 || c.clipH < 0)) { ++st.spansSkipped; continue; }
        Span sp;
        sp.clipOn = c.clipOn;
        if (c.clipOn) {
            const double fx0 = std::floor((double)c.clipX * dpi), fy0 = std::floor((double)c.clipY * dpi);
            const double fx1 = std::ceil(((double)c.clipX + c.clipW) * dpi), fy1 = std::ceil(((double)c.clipY + c.clipH) * dpi);
            sp.x0 = (int)std::max(0.0, std::min<double>(f.fbWidth, fx0)); sp.x1 = (int)std::max(0.0, std::min<double>(f.fbWidth, fx1));
            sp.y0 = (int)std::max(0.0, std::min<double>(f.fbHeight, fy0)); sp.y1 = (int)std::max(0.0, std::min<double>(f.fbHeight, fy1));
            if (sp.x0 >= sp.x1 || sp.y0 >= sp.y1) { ++st.spansSkipped; continue; }       // clipped away entirely
        }
        if (c.texture) {
            sp.tex = resolve(c.texture);
            if (!sp.tex) { ++st.unresolvedSpans; ++st.spansSkipped; continue; }
        }
        if (!repair) {
            sp.idxOffset = (int)rg.from; sp.idxCount = (int)(rg.to - rg.from);
            st.trianglesDrawn += sp.idxCount / 3;
        } else {
            sp.idxOffset = (int)m_idx.size();
            for (std::int64_t i = rg.from; i + 3 <= rg.to; i += 3) {
                const std::int32_t a = d.indices[i], b = d.indices[i + 1], cc = d.indices[i + 2];
                const unsigned n = (unsigned)d.vertexCount;
                if ((unsigned)a >= n || (unsigned)b >= n || (unsigned)cc >= n || badV[(size_t)a] || badV[(size_t)b] || badV[(size_t)cc]) { ++st.trianglesDropped; continue; }
                m_idx.push_back(a); m_idx.push_back(b); m_idx.push_back(cc);
            }
            sp.idxCount = (int)m_idx.size() - sp.idxOffset;
            if (sp.idxCount <= 0) continue;
            st.trianglesDrawn += sp.idxCount / 3;
        }
        m_spans.push_back(sp);
        ++st.spansDrawn;
    }
    if (repair) { idx = m_idx.data(); idxCount = (int)m_idx.size(); }
    if (m_spans.empty() || idxCount <= 0) return st;

    if (!m_b.BeginFrame(f.fbWidth, f.fbHeight, f.dpi)) { st.refused = true; return st; }
    m_b.Draw(vtx, d.vertexCount, idx, idxCount, m_spans.data(), (int)m_spans.size());
    m_b.EndFrame();
    return st;
}

} // namespace render
} // namespace OkayUI
