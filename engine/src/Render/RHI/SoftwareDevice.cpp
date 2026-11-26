// RHI Software backend: runs command lists on the CPU. It is always available and is the
// reference picture the other backends are compared with in the tests.
//
// Pipeline per triangle, as the OpenGL 2.1 / 3.3 specifications describe it (section
// "Rasterization" and "Per-Fragment Operations"): the shader's CPU vertex function gives a
// clip position and varyings; the triangle is clipped against the six clip planes
// (-w <= x, y, z <= w) in homogeneous space; the perspective divide and the viewport give
// window coordinates (y up); the triangle is culled by its winding; pixels whose centre
// (x + 0.5, y + 0.5) is inside are found with edge functions in fixed point (8 bits below
// the pixel), with a top-left style tie rule so two triangles sharing an edge never both
// draw a pixel (needed for blending: a full-screen quad must not add its diagonal twice);
// varyings are interpolated perspective-correctly, depth linearly in window space; then the
// depth test, the CPU fragment function, blending (factors on floats, the destination read
// as unsigned-normalized for RGBA8 and rounded to 16-bit floats for RGBA16F) and the
// colour write mask.
//
// The software renderer's own rasterizer (SoftwareRenderer, TriangleSpan.hpp) is not used
// here: its float edge tests accept a pixel on a shared edge for both triangles, which is
// right for its opaque depth passes but adds a shared edge twice when blending.
// Written by us from the specifications above; no code from any other project.
#include "okay/Render/RHI/RHI.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace okay {
namespace rhi {
namespace {

float RoundToHalf(float v) {   // the nearest IEEE half-precision value (what an RGBA16F target keeps)
    if (!(v == v)) return v;
    const float a = std::fabs(v);
    if (a >= 65520.0f) return v > 0 ? INFINITY : -INFINITY;
    float ulp;
    if (a < 6.103515625e-05f) ulp = 5.9604644775390625e-08f;   // 2^-24: subnormal halves
    else { int e = 0; std::frexp(a, &e); ulp = std::ldexp(1.0f, e - 11); }   // a in [2^(e-1), 2^e): 10 mantissa bits
    return std::nearbyint(v / ulp) * ulp;
}

struct SwBuffer { std::vector<std::uint8_t> bytes; BufferUsage usage = BufferUsage::Vertex; };
struct SwTexture {
    int w = 0, h = 0;
    Format fmt = Format::RGBA8;
    bool rt = false, sampled = true;
    std::uint32_t swapChain = 0;            // backbuffer of this swap chain (0: an ordinary texture)
    std::vector<std::uint8_t> u8;           // RGBA8
    std::vector<float> f;                   // float colour formats (4 per pixel) and Depth (1 per pixel)
};
struct SwShader { ShaderDesc desc; };
struct SwPipeline { PipelineDesc desc; };
struct SwSwapChain { std::uint32_t texture = 0; };

struct SampleBinding {
    const SwTexture* tex[kMaxTextureSlots] = {};
    SamplerDesc samp[kMaxTextureSlots];
};

void Texel(const SwTexture& t, int x, int y, float out[4]) {
    const std::size_t i = (std::size_t)y * t.w + x;
    if (t.fmt == Format::RGBA8) {
        const std::uint8_t* p = &t.u8[i * 4];
        for (int k = 0; k < 4; ++k) out[k] = p[k] * (1.0f / 255.0f);
    } else if (t.fmt == Format::Depth) {
        out[0] = out[1] = out[2] = t.f[i]; out[3] = 1.0f;
    } else {
        for (int k = 0; k < 4; ++k) out[k] = t.f[i * 4 + k];
    }
}

int WrapIndex(int i, int n, Wrap w) {
    if (w == Wrap::Repeat) { i %= n; return i < 0 ? i + n : i; }
    return i < 0 ? 0 : (i >= n ? n - 1 : i);
}

// texture2D() without mip levels: GL's texel centres are at (i + 0.5) / size.
void SampleFn(const void* ctx, int slot, float u, float v, float* rgba) {
    const SampleBinding& b = *static_cast<const SampleBinding*>(ctx);
    const SwTexture* t = (slot >= 0 && slot < kMaxTextureSlots) ? b.tex[slot] : nullptr;
    if (!t || t->w <= 0 || t->h <= 0) { rgba[0] = rgba[1] = rgba[2] = 0.0f; rgba[3] = 1.0f; return; }
    const SamplerDesc& s = b.samp[slot];
    const float x = u * (float)t->w, y = v * (float)t->h;
    if (s.magFilter == Filter::Nearest) {
        Texel(*t, WrapIndex((int)std::floor(x), t->w, s.wrapU), WrapIndex((int)std::floor(y), t->h, s.wrapV), rgba);
        return;
    }
    const float fx = x - 0.5f, fy = y - 0.5f;
    const float x0f = std::floor(fx), y0f = std::floor(fy);
    const float ax = fx - x0f, ay = fy - y0f;
    const int x0 = (int)x0f, y0 = (int)y0f;
    const int xa = WrapIndex(x0, t->w, s.wrapU), xb = WrapIndex(x0 + 1, t->w, s.wrapU);
    const int ya = WrapIndex(y0, t->h, s.wrapV), yb = WrapIndex(y0 + 1, t->h, s.wrapV);
    float c00[4], c10[4], c01[4], c11[4];
    Texel(*t, xa, ya, c00); Texel(*t, xb, ya, c10); Texel(*t, xa, yb, c01); Texel(*t, xb, yb, c11);
    for (int k = 0; k < 4; ++k) {
        const float top = c00[k] + (c10[k] - c00[k]) * ax, bot = c01[k] + (c11[k] - c01[k]) * ax;
        rgba[k] = top + (bot - top) * ay;
    }
}

float Factor(BlendFactor f, const float* src, const float* dst, int k) {
    switch (f) {
        case BlendFactor::Zero: return 0.0f;
        case BlendFactor::One: return 1.0f;
        case BlendFactor::SrcColor: return src[k];
        case BlendFactor::OneMinusSrcColor: return 1.0f - src[k];
        case BlendFactor::DstColor: return dst[k];
        case BlendFactor::OneMinusDstColor: return 1.0f - dst[k];
        case BlendFactor::SrcAlpha: return src[3];
        case BlendFactor::OneMinusSrcAlpha: return 1.0f - src[3];
        case BlendFactor::DstAlpha: return dst[3];
        case BlendFactor::OneMinusDstAlpha: return 1.0f - dst[3];
    }
    return 0.0f;
}

bool Compare(CompareOp op, float a, float b) {   // a: incoming, b: stored
    switch (op) {
        case CompareOp::Never: return false;
        case CompareOp::Less: return a < b;
        case CompareOp::Equal: return a == b;
        case CompareOp::LessEqual: return a <= b;
        case CompareOp::Greater: return a > b;
        case CompareOp::NotEqual: return a != b;
        case CompareOp::GreaterEqual: return a >= b;
        case CompareOp::Always: return true;
    }
    return true;
}

struct ClipVert { float pos[4]; float var[kMaxVaryings]; };

class SoftwareDevice final : public Device {
public:
    SoftwareDevice() {
        m_caps.backend = Backend::Software;
        m_caps.deviceName = "Software";
        m_caps.maxTextureSize = 16384;
        m_caps.floatRenderTargets = true;
        m_caps.depthTextures = true;
        m_caps.needsBarriers = false;
        m_caps.framesInFlight = 1;
    }
    ~SoftwareDevice() override { ReleaseAllDeferred(); }

    const Caps& GetCaps() const override { return m_caps; }

    BufferHandle CreateBuffer(const BufferDesc& d) override {
        if (d.size == 0) { SetError("CreateBuffer: size 0"); return {}; }
        SwBuffer b; b.usage = d.usage; b.bytes.assign(d.size, 0);
        if (d.initialData) std::memcpy(b.bytes.data(), d.initialData, d.size);
        BufferHandle h{NextId()}; m_buffers[h.id] = std::move(b); return h;
    }
    TextureHandle CreateTexture(const TextureDesc& d) override {
        if (d.width <= 0 || d.height <= 0 || d.width > m_caps.maxTextureSize || d.height > m_caps.maxTextureSize) {
            SetError("CreateTexture: bad size"); return {};
        }
        SwTexture t; t.w = d.width; t.h = d.height; t.fmt = d.format; t.rt = d.renderTarget; t.sampled = d.sampled;
        Allocate(t);
        TextureHandle h{NextId()};
        m_textures[h.id] = std::move(t);
        if (d.initialData) UpdateTexture(h, d.initialData);
        return h;
    }
    SamplerHandle CreateSampler(const SamplerDesc& d) override { SamplerHandle h{NextId()}; m_samplers[h.id] = d; return h; }
    ShaderHandle CreateShader(const ShaderDesc& d) override {
        const std::string err = ValidateShaderDesc(d);
        if (!err.empty()) { SetError(std::string("CreateShader: ") + err); return {}; }
        if (!d.cpuVertex || !d.cpuFragment) { SetError(std::string("CreateShader '") + d.debugName + "': no CPU functions for the Software backend"); return {}; }
        ShaderHandle h{NextId()}; m_shaders[h.id] = SwShader{d}; return h;
    }
    PipelineHandle CreatePipeline(const PipelineDesc& d) override {
        if (!m_shaders.count(d.shader.id)) { SetError("CreatePipeline: unknown shader"); return {}; }
        const std::string err = ValidatePipelineDesc(d);
        if (!err.empty()) { SetError("CreatePipeline: " + err); return {}; }
        PipelineHandle h{NextId()}; m_pipelines[h.id] = SwPipeline{d}; return h;
    }
    SwapChainHandle CreateSwapChain(int width, int height) override {
        TextureDesc td; td.width = width; td.height = height; td.renderTarget = true; td.sampled = false;
        TextureHandle t = CreateTexture(td);
        if (!t) return {};
        SwapChainHandle h{NextId()};
        m_textures[t.id].swapChain = h.id;
        m_swapChains[h.id] = SwSwapChain{t.id};
        return h;
    }

    bool UpdateBuffer(BufferHandle b, std::size_t offset, const void* data, std::size_t bytes) override {
        auto it = m_buffers.find(b.id);
        if (it == m_buffers.end() || offset + bytes > it->second.bytes.size()) { SetError("UpdateBuffer: bad buffer or range"); return false; }
        if (bytes) std::memcpy(it->second.bytes.data() + offset, data, bytes);
        return true;
    }
    bool UpdateTexture(TextureHandle h, const void* pixels) override {
        auto it = m_textures.find(h.id);
        if (it == m_textures.end() || !pixels) { SetError("UpdateTexture: bad texture"); return false; }
        SwTexture& t = it->second;
        const std::size_t n = (std::size_t)t.w * t.h;
        if (t.fmt == Format::RGBA8) std::memcpy(t.u8.data(), pixels, n * 4);
        else if (t.fmt == Format::Depth) std::memcpy(t.f.data(), pixels, n * sizeof(float));
        else {
            std::memcpy(t.f.data(), pixels, n * 4 * sizeof(float));
            if (t.fmt == Format::RGBA16F) for (float& v : t.f) v = RoundToHalf(v);
        }
        return true;
    }
    bool ReadTexture(TextureHandle h, std::vector<std::uint8_t>& out) override {
        auto it = m_textures.find(h.id);
        if (it == m_textures.end()) { SetError("ReadTexture: bad texture"); return false; }
        const SwTexture& t = it->second;
        if (t.fmt == Format::RGBA8) out = t.u8;
        else { out.resize(t.f.size() * sizeof(float)); std::memcpy(out.data(), t.f.data(), out.size()); }
        return true;
    }
    bool TextureSize(TextureHandle h, int& w, int& hh) const override {
        auto it = m_textures.find(h.id);
        if (it == m_textures.end()) return false;
        w = it->second.w; hh = it->second.h; return true;
    }
    TextureHandle BackBuffer(SwapChainHandle s) override {
        auto it = m_swapChains.find(s.id);
        return it == m_swapChains.end() ? TextureHandle{} : TextureHandle{it->second.texture};
    }
    bool ResizeSwapChain(SwapChainHandle s, int width, int height) override {
        auto it = m_swapChains.find(s.id);
        if (it == m_swapChains.end() || width <= 0 || height <= 0) return false;
        SwTexture& t = m_textures[it->second.texture];
        t.w = width; t.h = height; Allocate(t);
        return true;
    }
    bool Present(SwapChainHandle s) override { return m_swapChains.count(s.id) != 0; }   // the host reads the back buffer

    std::size_t LiveResources() const override {
        return m_buffers.size() + m_textures.size() + m_samplers.size() + m_shaders.size() + m_pipelines.size() + m_swapChains.size();
    }

    bool Submit(const CommandList& list) override;

protected:
    void Release(Kind k, std::uint32_t id) override {
        switch (k) {
            case Kind::Buffer: m_buffers.erase(id); break;
            case Kind::Texture: {
                auto it = m_textures.find(id);
                if (it != m_textures.end() && it->second.swapChain == 0) m_textures.erase(it);   // a back buffer goes with its swap chain
                break;
            }
            case Kind::Sampler: m_samplers.erase(id); break;
            case Kind::Shader: m_shaders.erase(id); break;
            case Kind::Pipeline: m_pipelines.erase(id); break;
            case Kind::SwapChain: {
                auto it = m_swapChains.find(id);
                if (it != m_swapChains.end()) { m_textures.erase(it->second.texture); m_swapChains.erase(it); }
                break;
            }
        }
    }

private:
    static void Allocate(SwTexture& t) {
        const std::size_t n = (std::size_t)t.w * t.h;
        t.u8.clear(); t.f.clear();
        if (t.fmt == Format::RGBA8) t.u8.assign(n * 4, 0);
        else if (t.fmt == Format::Depth) t.f.assign(n, 1.0f);
        else t.f.assign(n * 4, 0.0f);
    }

    // Per-submit state.
    struct State {
        SwTexture* color = nullptr; SwTexture* depth = nullptr;
        int vx = 0, vy = 0, vw = 0, vh = 0;
        const SwPipeline* pipe = nullptr; const SwShader* shader = nullptr;
        const SwBuffer* vb = nullptr; std::uint32_t vbOffset = 0;
        const SwBuffer* ib = nullptr; IndexType ibType = IndexType::U16; std::uint32_t ibOffset = 0;
        SampleBinding samples;
        const void* uniforms = nullptr;
    };
    bool FetchVertex(const State& s, std::uint32_t index, ClipVert& out);
    void DrawTriangle(const State& s, const ClipVert& a, const ClipVert& b, const ClipVert& c);
    void RasterTriangle(const State& s, const float* sx, const float* sy, const float* sz, const float* iw,
                        const float (*vw)[kMaxVaryings], int nvar);
    void WritePixel(const State& s, int x, int y, const float* col);

    Caps m_caps;
    std::unordered_map<std::uint32_t, SwBuffer> m_buffers;
    std::unordered_map<std::uint32_t, SwTexture> m_textures;
    std::unordered_map<std::uint32_t, SamplerDesc> m_samplers;
    std::unordered_map<std::uint32_t, SwShader> m_shaders;
    std::unordered_map<std::uint32_t, SwPipeline> m_pipelines;
    std::unordered_map<std::uint32_t, SwSwapChain> m_swapChains;
    std::string m_drawError;
};

bool SoftwareDevice::FetchVertex(const State& s, std::uint32_t index, ClipVert& out) {
    float attr[kMaxAttributes][4];
    for (auto& a : attr) { a[0] = a[1] = a[2] = 0.0f; a[3] = 1.0f; }
    const VertexLayout& L = s.pipe->desc.layout;
    for (const VertexAttribute& va : L.attributes) {
        if (!s.vb) { m_drawError = "draw: the pipeline has vertex attributes but no vertex buffer is set"; return false; }
        const std::size_t at = (std::size_t)s.vbOffset + (std::size_t)index * L.stride + va.offset;
        const int n = va.format == VertexFormat::Float1 ? 1 : va.format == VertexFormat::Float2 ? 2
                    : va.format == VertexFormat::Float3 ? 3 : 4;
        const std::size_t bytes = va.format == VertexFormat::UNorm8x4 ? 4 : (std::size_t)n * 4;
        if (at + bytes > s.vb->bytes.size()) { m_drawError = "draw: vertex read past the end of the vertex buffer"; return false; }
        const std::uint8_t* p = s.vb->bytes.data() + at;
        if (va.format == VertexFormat::UNorm8x4) for (int k = 0; k < 4; ++k) attr[va.location][k] = p[k] * (1.0f / 255.0f);
        else for (int k = 0; k < n; ++k) std::memcpy(&attr[va.location][k], p + k * 4, 4);
    }
    const ShaderDesc& sd = s.shader->desc;
    std::memset(out.var, 0, sizeof(out.var));
    sd.cpuVertex(attr, s.uniforms, sd.cpuUserData, out.pos, out.var);
    return true;
}

// Clip against the six planes of the clip volume (Sutherland-Hodgman in homogeneous
// space, varyings interpolated with the same parameter), then fan out the polygon.
void SoftwareDevice::DrawTriangle(const State& s, const ClipVert& a, const ClipVert& b, const ClipVert& c) {
    const int nvar = s.shader->desc.cpuVaryings;
    ClipVert bufA[16], bufB[16];
    ClipVert* in = bufA; ClipVert* out = bufB;
    int n = 3;
    in[0] = a; in[1] = b; in[2] = c;
    auto dist = [](const ClipVert& v, int plane) {
        const float w = v.pos[3];
        switch (plane) {
            case 0: return w + v.pos[0]; case 1: return w - v.pos[0];
            case 2: return w + v.pos[1]; case 3: return w - v.pos[1];
            case 4: return w + v.pos[2]; default: return w - v.pos[2];
        }
    };
    for (int plane = 0; plane < 6 && n > 0; ++plane) {
        int m = 0;
        for (int i = 0; i < n; ++i) {
            const ClipVert& p = in[i]; const ClipVert& q = in[(i + 1) % n];
            const float dp = dist(p, plane), dq = dist(q, plane);
            if (dp >= 0.0f) { if (m < 15) out[m++] = p; }
            if ((dp >= 0.0f) != (dq >= 0.0f) && m < 15) {
                const float t = dp / (dp - dq);
                ClipVert& r = out[m++];
                for (int k = 0; k < 4; ++k) r.pos[k] = p.pos[k] + (q.pos[k] - p.pos[k]) * t;
                for (int k = 0; k < nvar; ++k) r.var[k] = p.var[k] + (q.var[k] - p.var[k]) * t;
            }
        }
        std::swap(in, out); n = m;
    }
    if (n < 3) return;
    float sx[16], sy[16], sz[16], iw[16];
    float vw[16][kMaxVaryings];
    for (int i = 0; i < n; ++i) {
        const float w = in[i].pos[3];
        if (!(w > 1e-30f)) return;
        const float inv = 1.0f / w;
        sx[i] = (in[i].pos[0] * inv * 0.5f + 0.5f) * (float)s.vw + (float)s.vx;
        sy[i] = (in[i].pos[1] * inv * 0.5f + 0.5f) * (float)s.vh + (float)s.vy;
        sz[i] = in[i].pos[2] * inv * 0.5f + 0.5f;
        iw[i] = inv;
        for (int k = 0; k < nvar; ++k) vw[i][k] = in[i].var[k] * inv;
    }
    for (int i = 1; i + 1 < n; ++i) {
        const float tx[3] = {sx[0], sx[i], sx[i + 1]}, ty[3] = {sy[0], sy[i], sy[i + 1]};
        const float tz[3] = {sz[0], sz[i], sz[i + 1]}, tw[3] = {iw[0], iw[i], iw[i + 1]};
        float tv[3][kMaxVaryings];
        for (int k = 0; k < nvar; ++k) { tv[0][k] = vw[0][k]; tv[1][k] = vw[i][k]; tv[2][k] = vw[i + 1][k]; }
        RasterTriangle(s, tx, ty, tz, tw, tv, nvar);
    }
}

void SoftwareDevice::RasterTriangle(const State& s, const float* sx, const float* sy, const float* sz, const float* iw,
                                    const float (*vw)[kMaxVaryings], int nvar) {
    constexpr int kSub = 256;   // 8 sub-pixel bits
    std::int64_t X[3], Y[3];
    for (int i = 0; i < 3; ++i) { X[i] = std::llround((double)sx[i] * kSub); Y[i] = std::llround((double)sy[i] * kSub); }
    std::int64_t area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    if (area == 0) return;
    const bool front = area > 0;   // counter-clockwise in window space (y up)
    const CullMode cull = s.pipe->desc.cull;
    if ((cull == CullMode::Back && !front) || (cull == CullMode::Front && front)) return;
    int i0 = 0, i1 = 1, i2 = 2;
    if (!front) { std::swap(i1, i2); area = -area; }
    const int idx[3] = {i0, i1, i2};
    // Edge e goes from vertex idx[e] to idx[(e+1)%3]; its value is the weight of the vertex opposite.
    std::int64_t ax[3], ay[3], dx[3], dy[3];
    bool topLeft[3];
    for (int e = 0; e < 3; ++e) {
        const int a = idx[e], b = idx[(e + 1) % 3];
        ax[e] = X[a]; ay[e] = Y[a]; dx[e] = X[b] - X[a]; dy[e] = Y[b] - Y[a];
        topLeft[e] = dy[e] < 0 || (dy[e] == 0 && dx[e] < 0);   // left edges go down, top edges go left (CCW, y up)
    }
    std::int64_t minX = std::min({X[0], X[1], X[2]}), maxX = std::max({X[0], X[1], X[2]});
    std::int64_t minY = std::min({Y[0], Y[1], Y[2]}), maxY = std::max({Y[0], Y[1], Y[2]});
    int x0 = (int)std::max<std::int64_t>(minX / kSub - 1, s.vx), x1 = (int)std::min<std::int64_t>(maxX / kSub + 1, s.vx + s.vw - 1);
    int y0 = (int)std::max<std::int64_t>(minY / kSub - 1, s.vy), y1 = (int)std::min<std::int64_t>(maxY / kSub + 1, s.vy + s.vh - 1);
    const int tw = s.color ? s.color->w : s.depth->w, th = s.color ? s.color->h : s.depth->h;
    x0 = std::max(x0, 0); y0 = std::max(y0, 0); x1 = std::min(x1, tw - 1); y1 = std::min(y1, th - 1);
    if (x0 > x1 || y0 > y1) return;

    const ShaderDesc& sd = s.shader->desc;
    const PipelineDesc& pd = s.pipe->desc;
    FragmentContext fc;
    fc.uniforms = s.uniforms; fc.userData = sd.cpuUserData;
    fc.sampleFn = &SampleFn; fc.sampleCtx = &s.samples;
    const double invArea = 1.0 / (double)area;
    // Vertex opposite each edge: edge 0 (idx0->idx1) weighs idx2, edge 1 weighs idx0, edge 2 weighs idx1.
    const int opp[3] = {idx[2], idx[0], idx[1]};
    float var[kMaxVaryings];
    for (int y = y0; y <= y1; ++y) {
        const std::int64_t py = (std::int64_t)y * kSub + kSub / 2;
        for (int x = x0; x <= x1; ++x) {
            const std::int64_t px = (std::int64_t)x * kSub + kSub / 2;
            std::int64_t E[3];
            bool inside = true;
            for (int e = 0; e < 3; ++e) {
                E[e] = dx[e] * (py - ay[e]) - dy[e] * (px - ax[e]);
                if (E[e] < 0 || (E[e] == 0 && !topLeft[e])) { inside = false; break; }
            }
            if (!inside) continue;
            double bw[3] = {0, 0, 0};
            for (int e = 0; e < 3; ++e) bw[opp[e]] = (double)E[e] * invArea;
            const double z = bw[0] * sz[0] + bw[1] * sz[1] + bw[2] * sz[2];
            const float depth = (float)std::min(1.0, std::max(0.0, z));
            float* dst = nullptr;
            if (s.depth) {
                dst = &s.depth->f[(std::size_t)y * s.depth->w + x];
                if (pd.depth.test && !Compare(pd.depth.compare, depth, *dst)) continue;
            }
            const double oow = bw[0] * iw[0] + bw[1] * iw[1] + bw[2] * iw[2];
            const double pw = oow != 0.0 ? 1.0 / oow : 0.0;
            for (int k = 0; k < nvar; ++k)
                var[k] = (float)((bw[0] * vw[0][k] + bw[1] * vw[1][k] + bw[2] * vw[2][k]) * pw);
            fc.fragCoord[0] = (float)x + 0.5f; fc.fragCoord[1] = (float)y + 0.5f;
            float col[4] = {0, 0, 0, 1};
            if (!sd.cpuFragment(var, fc, col)) continue;
            if (dst && pd.depth.test && pd.depth.write) *dst = depth;
            if (s.color) WritePixel(s, x, y, col);
        }
    }
}

void SoftwareDevice::WritePixel(const State& s, int x, int y, const float* src0) {
    SwTexture& t = *s.color;
    const BlendState& bs = s.pipe->desc.blend;
    const std::size_t i = (std::size_t)y * t.w + x;
    const bool unorm = t.fmt == Format::RGBA8;
    float src[4], dst[4], out[4];
    for (int k = 0; k < 4; ++k) src[k] = unorm ? std::min(1.0f, std::max(0.0f, src0[k])) : src0[k];   // fixed-point buffers clamp
    Texel(t, x, y, dst);
    if (bs.enable) {
        for (int k = 0; k < 4; ++k) {
            const BlendFactor sf = k < 3 ? bs.srcColor : bs.srcAlpha, df = k < 3 ? bs.dstColor : bs.dstAlpha;
            out[k] = src[k] * Factor(sf, src, dst, k) + dst[k] * Factor(df, src, dst, k);
        }
    } else {
        for (int k = 0; k < 4; ++k) out[k] = src[k];
    }
    for (int k = 0; k < 4; ++k) {
        if (!(bs.writeMask & (1u << k))) continue;
        if (unorm) {
            const float v = std::min(1.0f, std::max(0.0f, out[k]));
            t.u8[i * 4 + k] = (std::uint8_t)std::lrint(v * 255.0f);
        } else {
            t.f[i * 4 + k] = t.fmt == Format::RGBA16F ? RoundToHalf(out[k]) : out[k];
        }
    }
}

bool SoftwareDevice::Submit(const CommandList& list) {
    if (!list.Error().empty()) { SetError("Submit: the list has an error: " + list.Error()); return false; }
    State s;
    m_drawError.clear();
    ++m_stats.submits;
    for (const CommandList::Command& c : list.Commands()) {
        switch (c.op) {
            case CommandList::Op::Transition: ++m_stats.transitions; break;   // nothing to do on the CPU
            case CommandList::Op::BeginPass: {
                const RenderPassDesc& p = list.Pass(c.a);
                s = State{};
                if (p.color) {
                    auto it = m_textures.find(p.color.id);
                    if (it == m_textures.end() || !it->second.rt || IsDepth(it->second.fmt)) { SetError("Submit: bad colour attachment"); return false; }
                    s.color = &it->second;
                }
                if (p.depth) {
                    auto it = m_textures.find(p.depth.id);
                    if (it == m_textures.end() || !IsDepth(it->second.fmt)) { SetError("Submit: bad depth attachment"); return false; }
                    s.depth = &it->second;
                }
                if (s.color && s.depth && (s.color->w != s.depth->w || s.color->h != s.depth->h)) { SetError("Submit: attachment sizes differ"); return false; }
                s.vw = s.color ? s.color->w : s.depth->w; s.vh = s.color ? s.color->h : s.depth->h;
                if (s.color && p.colorLoad == LoadOp::Clear) {
                    const std::size_t n = (std::size_t)s.color->w * s.color->h;
                    if (s.color->fmt == Format::RGBA8) {
                        std::uint8_t b[4];
                        for (int k = 0; k < 4; ++k) b[k] = (std::uint8_t)std::lrint(std::min(1.0f, std::max(0.0f, p.clearColor[k])) * 255.0f);
                        for (std::size_t i = 0; i < n; ++i) std::memcpy(&s.color->u8[i * 4], b, 4);
                    } else {
                        for (std::size_t i = 0; i < n; ++i)
                            for (int k = 0; k < 4; ++k)
                                s.color->f[i * 4 + k] = s.color->fmt == Format::RGBA16F ? RoundToHalf(p.clearColor[k]) : p.clearColor[k];
                    }
                }
                if (s.depth && p.depthLoad == LoadOp::Clear) std::fill(s.depth->f.begin(), s.depth->f.end(), std::min(1.0f, std::max(0.0f, p.clearDepth)));
                ++m_stats.passes;
                break;
            }
            case CommandList::Op::EndPass: s.color = nullptr; s.depth = nullptr; break;
            case CommandList::Op::Viewport:
                s.vx = (int)c.a; s.vy = (int)c.b; s.vw = (int)c.c; s.vh = (int)c.d; break;
            case CommandList::Op::Pipeline: {
                auto it = m_pipelines.find(c.a);
                if (it == m_pipelines.end()) { SetError("Submit: unknown pipeline"); return false; }
                auto sh = m_shaders.find(it->second.desc.shader.id);
                if (sh == m_shaders.end()) { SetError("Submit: the pipeline's shader was destroyed"); return false; }
                s.pipe = &it->second; s.shader = &sh->second;
                ++m_stats.pipelineBinds;
                break;
            }
            case CommandList::Op::VertexBuffer: {
                auto it = m_buffers.find(c.a);
                if (it == m_buffers.end()) { SetError("Submit: unknown vertex buffer"); return false; }
                s.vb = &it->second; s.vbOffset = c.b; break;
            }
            case CommandList::Op::IndexBuffer: {
                auto it = m_buffers.find(c.a);
                if (it == m_buffers.end()) { SetError("Submit: unknown index buffer"); return false; }
                s.ib = &it->second; s.ibType = (IndexType)c.b; s.ibOffset = c.c; break;
            }
            case CommandList::Op::Texture: {
                if (c.b == 0) { s.samples.tex[c.a] = nullptr; break; }
                auto it = m_textures.find(c.b);
                auto sm = m_samplers.find(c.c);
                if (it == m_textures.end() || sm == m_samplers.end()) { SetError("Submit: unknown texture or sampler"); return false; }
                if (!it->second.sampled) { SetError("Submit: texture was not made to be sampled"); return false; }
                s.samples.tex[c.a] = &it->second; s.samples.samp[c.a] = sm->second; break;
            }
            case CommandList::Op::Uniforms: s.uniforms = c.b ? list.UniformData(c.a) : nullptr; break;
            case CommandList::Op::Draw:
            case CommandList::Op::DrawIndexed: {
                if (!s.pipe || (!s.color && !s.depth)) { SetError("Submit: draw without a pass or pipeline"); return false; }
                const bool indexed = c.op == CommandList::Op::DrawIndexed;
                if (indexed && !s.ib) { SetError("Submit: DrawIndexed without an index buffer"); return false; }
                const std::uint32_t count = c.a - c.a % 3;
                ++m_stats.draws;
                m_stats.trianglesIn += count / 3;
                for (std::uint32_t i = 0; i < count; i += 3) {
                    ClipVert v[3];
                    for (int k = 0; k < 3; ++k) {
                        std::uint32_t vi = c.b + i + (std::uint32_t)k;
                        if (indexed) {
                            const std::size_t isz = s.ibType == IndexType::U16 ? 2 : 4;
                            const std::size_t at = (std::size_t)s.ibOffset + (std::size_t)vi * isz;
                            if (at + isz > s.ib->bytes.size()) { SetError("Submit: index read past the end of the index buffer"); return false; }
                            if (isz == 2) { std::uint16_t x; std::memcpy(&x, &s.ib->bytes[at], 2); vi = x; }
                            else std::memcpy(&vi, &s.ib->bytes[at], 4);
                            vi = (std::uint32_t)((std::int64_t)vi + (std::int32_t)c.c);
                        }
                        if (!FetchVertex(s, vi, v[k])) { SetError("Submit: " + m_drawError); return false; }
                    }
                    DrawTriangle(s, v[0], v[1], v[2]);
                }
                break;
            }
        }
    }
    return true;
}

} // namespace

std::unique_ptr<Device> CreateSoftwareDevice() { return std::unique_ptr<Device>(new SoftwareDevice()); }

} // namespace rhi
} // namespace okay
