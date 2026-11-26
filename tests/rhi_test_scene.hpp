#pragma once
// The RHI test scene and picture helpers, shared by tests/test_rhi.cpp (Software, headless) and
// tests/test_rhi_gl.cpp (OpenGL vs Software under Xvfb). The scene uses every round 1 feature:
// a clear, a non-indexed triangle with vertex colours, an indexed cube with perspective, depth
// test and back-face culling, a textured quad (nearest and linear samplers, repeat wrap), and an
// alpha-blended quad over everything.
#include "okay/Render/RHI/RHI.hpp"
#include "okay/Render/RHI/SurfaceShader.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Quat.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace rhitest {
using namespace okay;

// ---- A vertex-colour shader: aPos vec3, aColor (UNorm8x4); uMVP ----
inline void ColorVert(const float (*a)[4], const void* u, const void*, float clip[4], float* var) {
    const float* m = static_cast<const float*>(u);
    for (int r = 0; r < 4; ++r) clip[r] = m[r] * a[0][0] + m[4 + r] * a[0][1] + m[8 + r] * a[0][2] + m[12 + r];
    for (int k = 0; k < 4; ++k) var[k] = a[1][k];
}
inline bool ColorFrag(const float* v, const rhi::FragmentContext&, float out[4]) {
    for (int k = 0; k < 4; ++k) out[k] = v[k];
    return true;
}
inline rhi::ShaderDesc ColorShader() {
    rhi::ShaderDesc d;
    d.debugName = "test colour";
    d.attributes = {"aPos", "aColor"};
    d.uniforms = {rhi::UniformField{"uMVP", rhi::UniformType::Mat4, 0}};
    d.uniformBytes = 64;
    d.glslVertex =
        "#version 120\n"
        "attribute vec3 aPos; attribute vec4 aColor; uniform mat4 uMVP; varying vec4 vCol;\n"
        "void main(){ vCol = aColor; gl_Position = uMVP * vec4(aPos, 1.0); }\n";
    d.glslFragment = "#version 120\nvarying vec4 vCol;\nvoid main(){ gl_FragColor = vCol; }\n";
    d.cpuVertex = &ColorVert; d.cpuFragment = &ColorFrag; d.cpuVaryings = 4;
    return d;
}

// ---- A textured shader: aPos vec3, aUV vec2; uMVP, uTint vec4; texture uTex ----
inline void TexVert(const float (*a)[4], const void* u, const void*, float clip[4], float* var) {
    const float* m = static_cast<const float*>(u);
    for (int r = 0; r < 4; ++r) clip[r] = m[r] * a[0][0] + m[4 + r] * a[0][1] + m[8 + r] * a[0][2] + m[12 + r];
    var[0] = a[1][0]; var[1] = a[1][1];
}
inline bool TexFrag(const float* v, const rhi::FragmentContext& ctx, float out[4]) {
    const float* tint = static_cast<const float*>(ctx.uniforms) + 16;
    float c[4]; ctx.Sample(0, v[0], v[1], c);
    for (int k = 0; k < 4; ++k) out[k] = c[k] * tint[k];
    return true;
}
inline rhi::ShaderDesc TexturedShader() {
    rhi::ShaderDesc d;
    d.debugName = "test textured";
    d.attributes = {"aPos", "aUV"};
    d.uniforms = {rhi::UniformField{"uMVP", rhi::UniformType::Mat4, 0}, rhi::UniformField{"uTint", rhi::UniformType::Vec4, 64}};
    d.uniformBytes = 80;
    d.textures = {"uTex"};
    d.glslVertex =
        "#version 120\n"
        "attribute vec3 aPos; attribute vec2 aUV; uniform mat4 uMVP; varying vec2 vUV;\n"
        "void main(){ vUV = aUV; gl_Position = uMVP * vec4(aPos, 1.0); }\n";
    d.glslFragment =
        "#version 120\nuniform sampler2D uTex; uniform vec4 uTint; varying vec2 vUV;\n"
        "void main(){ gl_FragColor = texture2D(uTex, vUV) * uTint; }\n";
    d.cpuVertex = &TexVert; d.cpuFragment = &TexFrag; d.cpuVaryings = 2;
    return d;
}

struct ColorVertex { float p[3]; std::uint8_t c[4]; };
struct TexVertex { float p[3]; float uv[2]; };

/// The cube: 24 vertices (4 per face, one colour per face), 36 U16 indices, counter-clockwise from outside.
inline void BuildCube(std::vector<ColorVertex>& v, std::vector<std::uint16_t>& idx) {
    const float n[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const std::uint8_t col[6][4] = {{230, 60, 50, 255}, {60, 200, 70, 255}, {70, 90, 230, 255},
                                    {230, 210, 60, 255}, {200, 70, 210, 255}, {60, 210, 220, 255}};
    for (int f = 0; f < 6; ++f) {
        const float* N = n[f];
        const bool yFace = std::fabs(N[1]) > 0.5f;
        const float up[3] = {0.0f, yFace ? 0.0f : 1.0f, yFace ? 1.0f : 0.0f};
        const float U[3] = {up[1] * N[2] - up[2] * N[1], up[2] * N[0] - up[0] * N[2], up[0] * N[1] - up[1] * N[0]};   // up x N
        const float s[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        const std::uint16_t base = (std::uint16_t)v.size();
        for (int k = 0; k < 4; ++k) {
            ColorVertex cv;
            for (int a = 0; a < 3; ++a) cv.p[a] = N[a] + U[a] * s[k][0] + up[a] * s[k][1];
            for (int a = 0; a < 4; ++a) cv.c[a] = col[f][a];
            v.push_back(cv);
        }
        const std::uint16_t t[6] = {0, 1, 2, 0, 2, 3};
        for (std::uint16_t i : t) idx.push_back((std::uint16_t)(base + i));
    }
}

/// Draw the scene into a W x H RGBA8 target on `dev` and read it back (row 0 = bottom).
inline bool DrawTestScene(rhi::Device& dev, int W, int H, std::vector<std::uint8_t>& out, std::string* err = nullptr) {
    auto failWith = [&](const std::string& e) { if (err) *err = e + " (" + dev.LastError() + ")"; return false; };
    rhi::ShaderHandle cs = dev.CreateShader(ColorShader()), ts = dev.CreateShader(TexturedShader());
    if (!cs || !ts) return failWith("shader");
    rhi::PipelineDesc pd;
    pd.shader = cs;
    pd.layout.stride = sizeof(ColorVertex);
    pd.layout.attributes = {rhi::VertexAttribute{0, rhi::VertexFormat::Float3, 0}, rhi::VertexAttribute{1, rhi::VertexFormat::UNorm8x4, 12}};
    rhi::PipelineHandle flat = dev.CreatePipeline(pd);
    pd.depth.test = true; pd.depth.write = true; pd.depth.compare = rhi::CompareOp::Less; pd.cull = rhi::CullMode::Back;
    rhi::PipelineHandle solid = dev.CreatePipeline(pd);
    pd.depth = rhi::DepthState{}; pd.cull = rhi::CullMode::None; pd.blend = rhi::BlendState::Alpha();
    rhi::PipelineHandle blended = dev.CreatePipeline(pd);
    rhi::PipelineDesc tp;
    tp.shader = ts;
    tp.layout.stride = sizeof(TexVertex);
    tp.layout.attributes = {rhi::VertexAttribute{0, rhi::VertexFormat::Float3, 0}, rhi::VertexAttribute{1, rhi::VertexFormat::Float2, 12}};
    rhi::PipelineHandle textured = dev.CreatePipeline(tp);
    if (!flat || !solid || !blended || !textured) return failWith("pipeline");

    const ColorVertex tri[3] = {{{-0.95f, -0.95f, 0}, {255, 40, 40, 255}}, {{0.2f, -0.9f, 0}, {40, 255, 40, 255}},
                                {{-0.6f, 0.3f, 0}, {40, 40, 255, 255}}};
    const ColorVertex glass[6] = {{{-0.3f, -0.5f, 0}, {255, 255, 255, 110}}, {{0.9f, -0.5f, 0}, {255, 255, 255, 110}},
                                  {{0.9f, 0.1f, 0}, {255, 200, 120, 160}}, {{-0.3f, -0.5f, 0}, {255, 255, 255, 110}},
                                  {{0.9f, 0.1f, 0}, {255, 200, 120, 160}}, {{-0.3f, 0.1f, 0}, {255, 200, 120, 160}}};
    const TexVertex quad[6] = {{{0.35f, 0.35f, 0}, {0, 0}}, {{0.95f, 0.35f, 0}, {2, 0}}, {{0.95f, 0.95f, 0}, {2, 2}},
                               {{0.35f, 0.35f, 0}, {0, 0}}, {{0.95f, 0.95f, 0}, {2, 2}}, {{0.35f, 0.95f, 0}, {0, 2}}};
    std::vector<ColorVertex> cube; std::vector<std::uint16_t> cubeIdx;
    BuildCube(cube, cubeIdx);
    rhi::BufferDesc bd;
    bd.size = sizeof(tri); bd.initialData = tri; rhi::BufferHandle triVb = dev.CreateBuffer(bd);
    bd.size = sizeof(glass); bd.initialData = glass; rhi::BufferHandle glassVb = dev.CreateBuffer(bd);
    bd.size = sizeof(quad); bd.initialData = quad; rhi::BufferHandle quadVb = dev.CreateBuffer(bd);
    bd.size = cube.size() * sizeof(ColorVertex); bd.initialData = cube.data(); rhi::BufferHandle cubeVb = dev.CreateBuffer(bd);
    bd.usage = rhi::BufferUsage::Index; bd.size = cubeIdx.size() * 2; bd.initialData = cubeIdx.data();
    rhi::BufferHandle cubeIb = dev.CreateBuffer(bd);
    // A 4x4 checker (two colours) for the textured quad, read with a nearest sampler in repeat mode.
    std::uint8_t checker[4 * 4 * 4];
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const bool a = ((x + y) & 1) == 0;
            std::uint8_t* p = &checker[(y * 4 + x) * 4];
            p[0] = a ? 250 : 30; p[1] = a ? 240 : 60; p[2] = a ? 90 : 160; p[3] = 255;
        }
    rhi::TextureDesc td; td.width = 4; td.height = 4; td.initialData = checker;
    rhi::TextureHandle tex = dev.CreateTexture(td);
    rhi::SamplerDesc sd; sd.minFilter = sd.magFilter = rhi::Filter::Nearest; sd.wrapU = sd.wrapV = rhi::Wrap::Repeat;
    rhi::SamplerHandle nearest = dev.CreateSampler(sd);
    rhi::TextureDesc cd; cd.width = W; cd.height = H; cd.renderTarget = true;
    rhi::TextureHandle color = dev.CreateTexture(cd);
    cd.format = rhi::Format::Depth;
    rhi::TextureHandle depth = dev.CreateTexture(cd);
    if (!triVb || !glassVb || !quadVb || !cubeVb || !cubeIb || !tex || !nearest || !color || !depth) return failWith("resource");

    const Mat4 ident;
    const Vec3 eye{2.2f, 1.8f, 3.4f};
    const Mat4 vp = Mat4::Perspective(50.0f, (float)W / (float)H, 0.5f, 20.0f) * Mat4::LookAt(eye, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    const Mat4 cubeMvp = vp * Mat4::Translate(Vec3{0.3f, 0.1f, 0}) * Mat4::Rotate(Quat::Euler(0, -35, 0));
    float texU[20]; for (int i = 0; i < 16; ++i) texU[i] = ident.m[i];
    texU[16] = 1.0f; texU[17] = 1.0f; texU[18] = 1.0f; texU[19] = 1.0f;

    rhi::CommandList cl;
    rhi::RenderPassDesc rp; rp.color = color; rp.depth = depth;
    rp.colorLoad = rhi::LoadOp::Clear; rp.depthLoad = rhi::LoadOp::Clear;
    rp.clearColor[0] = 0.08f; rp.clearColor[1] = 0.10f; rp.clearColor[2] = 0.18f; rp.clearColor[3] = 1.0f;
    cl.BeginRenderPass(rp);
    cl.SetPipeline(flat); cl.SetVertexBuffer(triVb); cl.SetUniforms(ident.m, 64); cl.Draw(3);
    cl.SetPipeline(solid); cl.SetVertexBuffer(cubeVb); cl.SetIndexBuffer(cubeIb, rhi::IndexType::U16);
    cl.SetUniforms(cubeMvp.m, 64); cl.DrawIndexed((std::uint32_t)cubeIdx.size());
    cl.SetPipeline(textured); cl.SetVertexBuffer(quadVb); cl.SetTexture(0, tex, nearest); cl.SetUniforms(texU, sizeof(texU)); cl.Draw(6);
    cl.SetPipeline(blended); cl.SetVertexBuffer(glassVb); cl.SetUniforms(ident.m, 64); cl.Draw(6);
    cl.EndRenderPass();
    const bool ok = dev.Submit(cl) && dev.ReadTexture(color, out);
    for (rhi::PipelineHandle p : {flat, solid, blended, textured}) dev.Destroy(p);
    dev.Destroy(cs); dev.Destroy(ts);
    for (rhi::BufferHandle b : {triVb, glassVb, quadVb, cubeVb, cubeIb}) dev.Destroy(b);
    dev.Destroy(tex); dev.Destroy(nearest); dev.Destroy(color); dev.Destroy(depth);
    return ok ? true : failWith("submit/read");
}

/// An OkayShader surface (RHI shader from rhi::ShaderFromSurface) on a full-screen quad, uv 0..1 across it, into a
/// W x H RGBA8 target, read back. Everything made is destroyed again.
inline bool DrawSurfaceQuad(rhi::Device& d, const rhi::ShaderDesc& sd, const std::vector<std::uint8_t>& uniforms, int W, int H,
                            std::vector<std::uint8_t>& out) {
    struct V { float p[3], n[3], uv[2], c[3]; };
    const V q[6] = {{{-1, -1, 0}, {0, 0, 1}, {0, 0}, {1, 1, 1}}, {{1, -1, 0}, {0, 0, 1}, {1, 0}, {1, 1, 1}}, {{1, 1, 0}, {0, 0, 1}, {1, 1}, {1, 1, 1}},
                    {{-1, -1, 0}, {0, 0, 1}, {0, 0}, {1, 1, 1}}, {{1, 1, 0}, {0, 0, 1}, {1, 1}, {1, 1, 1}}, {{-1, 1, 0}, {0, 0, 1}, {0, 1}, {1, 1, 1}}};
    rhi::ShaderHandle sh = d.CreateShader(sd);
    rhi::BufferDesc bd; bd.size = sizeof(q); bd.initialData = q;
    rhi::BufferHandle vb = d.CreateBuffer(bd);
    rhi::PipelineDesc pd; pd.shader = sh; pd.layout.stride = sizeof(V);
    pd.layout.attributes = {rhi::VertexAttribute{0, rhi::VertexFormat::Float3, 0}, rhi::VertexAttribute{1, rhi::VertexFormat::Float3, 12},
                            rhi::VertexAttribute{2, rhi::VertexFormat::Float2, 24}, rhi::VertexAttribute{3, rhi::VertexFormat::Float3, 32}};
    rhi::PipelineHandle p = d.CreatePipeline(pd);
    rhi::TextureDesc td; td.width = W; td.height = H; td.renderTarget = true;
    rhi::TextureHandle t = d.CreateTexture(td);
    rhi::CommandList cl; rhi::RenderPassDesc rp; rp.color = t; rp.colorLoad = rhi::LoadOp::Clear;
    cl.BeginRenderPass(rp); cl.SetPipeline(p); cl.SetVertexBuffer(vb);
    cl.SetUniforms(uniforms.data(), (std::uint32_t)uniforms.size()); cl.Draw(6); cl.EndRenderPass();
    const bool ok = sh && vb && p && t && d.Submit(cl) && d.ReadTexture(t, out);
    d.Destroy(p); d.Destroy(sh); d.Destroy(vb); d.Destroy(t);
    return ok;
}

// ---- Picture helpers ----
struct Diff { int maxDiff = 0; double mean = 0; int over = 0; };
/// Per-channel comparison of two RGBA8 pictures; `over` counts pixels with any channel off by more than `thr`.
inline Diff Compare(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b, int thr) {
    Diff d;
    if (a.size() != b.size() || a.empty()) { d.maxDiff = 255; d.over = 1 << 30; return d; }
    double sum = 0;
    for (std::size_t i = 0; i < a.size(); i += 4) {
        int px = 0;
        for (int k = 0; k < 4; ++k) {
            const int e = std::abs((int)a[i + k] - (int)b[i + k]);
            px = e > px ? e : px; sum += e;
        }
        d.maxDiff = px > d.maxDiff ? px : d.maxDiff;
        if (px > thr) ++d.over;
    }
    d.mean = sum / (double)a.size();
    return d;
}
/// Save an RHI read-back (row 0 = bottom) as an upright PNG.
inline bool SaveUpright(const std::string& path, const std::vector<std::uint8_t>& px, int w, int h) {
    Image img(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int k = 0; k < 4; ++k) img.Data()[((std::size_t)y * w + x) * 4 + k] = px[((std::size_t)(h - 1 - y) * w + x) * 4 + k];
    return img.SavePNG(path);
}
/// Load an upright PNG back into RHI row order (row 0 = bottom).
inline bool LoadUpright(const std::string& path, std::vector<std::uint8_t>& px, int& w, int& h) {
    Image img;
    if (!img.Load(path)) return false;
    w = img.Width(); h = img.Height();
    px.assign((std::size_t)w * h * 4, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int k = 0; k < 4; ++k) px[((std::size_t)(h - 1 - y) * w + x) * 4 + k] = img.Data()[((std::size_t)y * w + x) * 4 + k];
    return true;
}

} // namespace rhitest
