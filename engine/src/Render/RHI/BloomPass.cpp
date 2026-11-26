// The bloom pass on the RHI: one description of the pass, GLSL for the OpenGL backend, HLSL for
// the Direct3D 11 backend and the same maths as C++ functions for the Software backend. Moved here from the OpenGL
// renderer's own bloom (GLRenderer.cpp, kBrightFrag / kAddFrag, deleted in the same
// change). The 9-tap gaussian uses 5 linear-filtered taps: offsets 1.384615 and 3.230769
// texels with weights 0.227027, 0.316216 and 0.070270 (binomial weights merged in pairs).
// Written by us; see BloomPass.hpp.
#include "okay/Render/RHI/BloomPass.hpp"

#include <cmath>

namespace okay {
namespace rhi {

const char* const kFullscreenVertGlsl =
    "#version 120\n"
    "attribute vec2 aPos; varying vec2 vUV;\n"
    "void main(){ vUV = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n";

const char* const kGaussianBlurGlsl =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform vec2 uDir; varying vec2 vUV;\n"   // uDir = one texel H or V
    "void main(){\n"                                                   // 9-tap gaussian (linear-sampled 5-tap)
    "  vec3 s = texture2D(uSrc, vUV).rgb * 0.227027;\n"
    "  s += (texture2D(uSrc, vUV + uDir * 1.384615).rgb + texture2D(uSrc, vUV - uDir * 1.384615).rgb) * 0.316216;\n"
    "  s += (texture2D(uSrc, vUV + uDir * 3.230769).rgb + texture2D(uSrc, vUV - uDir * 3.230769).rgb) * 0.070270;\n"
    "  gl_FragColor = vec4(s, 1.0);\n"
    "}\n";

namespace {

const char* const kBrightGlsl =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform float uThresh; varying vec2 vUV;\n"
    "void main(){\n"
    "  vec3 c = texture2D(uSrc, vUV).rgb;\n"
    "  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "  float k = max(l - uThresh, 0.0) / max(l, 1e-4);\n"   // keep only the over-threshold part
    "  gl_FragColor = vec4(c * k, 1.0);\n"
    "}\n";

const char* const kAddGlsl =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform float uStrength; varying vec2 vUV;\n"
    "void main(){ gl_FragColor = vec4(texture2D(uSrc, vUV).rgb * uStrength, 0.0); }\n";

// ---- The same programs in HLSL for the Direct3D 11 backend (RHI rules: TEXCOORDn inputs, cbuffer b0,
// texture slot i at t i / s i, rhiClip for the position). Each pixel shader is the GLSL above, line for line.
const char* const kFullscreenVertHlsl =
    "struct VOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "VOut VSMain(float2 aPos : TEXCOORD0) {\n"
    "  VOut o; o.uv = aPos * 0.5 + 0.5; o.pos = rhiClip(float4(aPos, 0.0, 1.0)); return o;\n"
    "}\n";

const char* const kBrightHlsl =
    "cbuffer U : register(b0) { float uThresh; };\n"   // dup-allow: an RHI pass shader; its HLSL lives with its GLSL and C++ twins (docs/rendering/rhi.md)
    "Texture2D uSrc : register(t0); SamplerState uSrcS : register(s0);\n"
    "float4 PSMain(float4 pos : SV_Position, float2 vUV : TEXCOORD0) : SV_Target {\n"
    "  float3 c = uSrc.Sample(uSrcS, vUV).rgb;\n"
    "  float l = dot(c, float3(0.2126, 0.7152, 0.0722));\n"
    "  float k = max(l - uThresh, 0.0) / max(l, 1e-4);\n"
    "  return float4(c * k, 1.0);\n"
    "}\n";

const char* const kBlurHlsl =
    "cbuffer U : register(b0) { float2 uDir; };\n"   // dup-allow: an RHI pass shader; its HLSL lives with its GLSL and C++ twins (docs/rendering/rhi.md)
    "Texture2D uSrc : register(t0); SamplerState uSrcS : register(s0);\n"
    "float4 PSMain(float4 pos : SV_Position, float2 vUV : TEXCOORD0) : SV_Target {\n"
    "  float3 s = uSrc.Sample(uSrcS, vUV).rgb * 0.227027;\n"
    "  s += (uSrc.Sample(uSrcS, vUV + uDir * 1.384615).rgb + uSrc.Sample(uSrcS, vUV - uDir * 1.384615).rgb) * 0.316216;\n"
    "  s += (uSrc.Sample(uSrcS, vUV + uDir * 3.230769).rgb + uSrc.Sample(uSrcS, vUV - uDir * 3.230769).rgb) * 0.070270;\n"
    "  return float4(s, 1.0);\n"
    "}\n";

const char* const kAddHlsl =
    "cbuffer U : register(b0) { float uStrength; };\n"   // dup-allow: an RHI pass shader; its HLSL lives with its GLSL and C++ twins (docs/rendering/rhi.md)
    "Texture2D uSrc : register(t0); SamplerState uSrcS : register(s0);\n"
    "float4 PSMain(float4 pos : SV_Position, float2 vUV : TEXCOORD0) : SV_Target {\n"
    "  return float4(uSrc.Sample(uSrcS, vUV).rgb * uStrength, 0.0);\n"
    "}\n";

// ---- The same programs for the Software backend ----
void FullscreenVert(const float (*attr)[4], const void*, const void*, float clip[4], float* var) {
    clip[0] = attr[0][0]; clip[1] = attr[0][1]; clip[2] = 0.0f; clip[3] = 1.0f;
    var[0] = attr[0][0] * 0.5f + 0.5f; var[1] = attr[0][1] * 0.5f + 0.5f;
}
bool BrightFrag(const float* v, const FragmentContext& ctx, float out[4]) {
    const float thresh = *static_cast<const float*>(ctx.uniforms);
    float c[4]; ctx.Sample(0, v[0], v[1], c);
    const float l = c[0] * 0.2126f + c[1] * 0.7152f + c[2] * 0.0722f;
    const float k = std::fmax(l - thresh, 0.0f) / std::fmax(l, 1e-4f);
    out[0] = c[0] * k; out[1] = c[1] * k; out[2] = c[2] * k; out[3] = 1.0f;
    return true;
}
bool BlurFrag(const float* v, const FragmentContext& ctx, float out[4]) {
    const float* dir = static_cast<const float*>(ctx.uniforms);
    static const float kOff[2] = {1.384615f, 3.230769f}, kW[2] = {0.316216f, 0.070270f};
    float c[4]; ctx.Sample(0, v[0], v[1], c);
    float s[3] = {c[0] * 0.227027f, c[1] * 0.227027f, c[2] * 0.227027f};
    for (int t = 0; t < 2; ++t) {
        float p[4], q[4];
        ctx.Sample(0, v[0] + dir[0] * kOff[t], v[1] + dir[1] * kOff[t], p);
        ctx.Sample(0, v[0] - dir[0] * kOff[t], v[1] - dir[1] * kOff[t], q);
        for (int k = 0; k < 3; ++k) s[k] += (p[k] + q[k]) * kW[t];
    }
    out[0] = s[0]; out[1] = s[1]; out[2] = s[2]; out[3] = 1.0f;
    return true;
}
bool AddFrag(const float* v, const FragmentContext& ctx, float out[4]) {
    const float strength = *static_cast<const float*>(ctx.uniforms);
    float c[4]; ctx.Sample(0, v[0], v[1], c);
    out[0] = c[0] * strength; out[1] = c[1] * strength; out[2] = c[2] * strength; out[3] = 0.0f;
    return true;
}

ShaderDesc QuadShader(const char* name, const char* frag, const char* hlsl, CpuFragmentFn cpu, const char* uniform, UniformType type) {
    ShaderDesc d;
    d.debugName = name;
    d.attributes = {"aPos"};
    d.uniforms = {UniformField{uniform, type, 0}};
    d.uniformBytes = 16;
    d.textures = {"uSrc"};
    d.glslVertex = kFullscreenVertGlsl;
    d.glslFragment = frag;
    d.hlslVertex = kFullscreenVertHlsl;
    d.hlslFragment = hlsl;
    d.cpuVertex = &FullscreenVert;
    d.cpuFragment = cpu;
    d.cpuVaryings = 2;
    return d;
}

} // namespace

bool BloomPass::Init(Device& dev) {
    Shutdown();
    m_brightSh = dev.CreateShader(QuadShader("bloom bright", kBrightGlsl, kBrightHlsl, &BrightFrag, "uThresh", UniformType::Float));
    m_blurSh = dev.CreateShader(QuadShader("bloom blur", kGaussianBlurGlsl, kBlurHlsl, &BlurFrag, "uDir", UniformType::Vec2));
    m_addSh = dev.CreateShader(QuadShader("bloom add", kAddGlsl, kAddHlsl, &AddFrag, "uStrength", UniformType::Float));
    static const float quad[12] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};   // two triangles, counter-clockwise
    BufferDesc bd; bd.size = sizeof(quad); bd.initialData = quad; bd.debugName = "bloom quad";
    m_quad = dev.CreateBuffer(bd);
    m_linear = dev.CreateSampler(SamplerDesc{});
    m_dev = &dev;
    if (!m_brightSh || !m_blurSh || !m_addSh || !m_quad || !m_linear) { Shutdown(); return false; }
    PipelineDesc pd;
    pd.layout.stride = 8;
    pd.layout.attributes = {VertexAttribute{0, VertexFormat::Float2, 0}};
    pd.shader = m_brightSh; pd.debugName = "bloom bright"; m_bright = dev.CreatePipeline(pd);
    pd.shader = m_blurSh; pd.debugName = "bloom blur"; m_blur = dev.CreatePipeline(pd);
    pd.shader = m_addSh; pd.debugName = "bloom add"; pd.blend = BlendState::Additive(); m_add = dev.CreatePipeline(pd);
    if (!m_bright || !m_blur || !m_add) { Shutdown(); return false; }
    return true;
}

void BloomPass::Shutdown() {
    if (!m_dev) return;
    Device& d = *m_dev;
    d.Destroy(m_bright); d.Destroy(m_blur); d.Destroy(m_add);
    d.Destroy(m_brightSh); d.Destroy(m_blurSh); d.Destroy(m_addSh);
    d.Destroy(m_quad); d.Destroy(m_linear);
    for (Targets& t : m_targets) { d.Destroy(t.a); d.Destroy(t.b); }
    m_targets.clear();
    m_bright = m_blur = m_add = PipelineHandle{};
    m_brightSh = m_blurSh = m_addSh = ShaderHandle{};
    m_quad = BufferHandle{}; m_linear = SamplerHandle{};
    m_list.Reset();
    m_dev = nullptr;
}

BloomPass::Targets* BloomPass::EnsureTargets(int hw, int hh) {
    ++m_use;
    for (Targets& t : m_targets)
        if (t.hw == hw && t.hh == hh) { t.lastUse = m_use; return &t; }
    if ((int)m_targets.size() >= kMaxSizes) {   // forget the size used longest ago
        std::size_t old = 0;
        for (std::size_t i = 1; i < m_targets.size(); ++i) if (m_targets[i].lastUse < m_targets[old].lastUse) old = i;
        m_dev->Destroy(m_targets[old].a); m_dev->Destroy(m_targets[old].b);
        m_targets.erase(m_targets.begin() + (std::ptrdiff_t)old);
    }
    Targets t; t.hw = hw; t.hh = hh; t.lastUse = m_use;
    TextureDesc td; td.width = hw; td.height = hh; td.format = Format::RGBA8; td.renderTarget = true;
    td.debugName = "bloom A"; t.a = m_dev->CreateTexture(td);
    td.debugName = "bloom B"; t.b = m_dev->CreateTexture(td);
    if (!t.a || !t.b) { m_dev->Destroy(t.a); m_dev->Destroy(t.b); return nullptr; }
    m_targets.push_back(t);
    return &m_targets.back();
}

bool BloomPass::Apply(TextureHandle frame, int w, int h, float threshold, float intensity) {
    if (!m_dev || !frame || w <= 0 || h <= 0) return false;
    const int hw = w / 2 > 0 ? w / 2 : 1, hh = h / 2 > 0 ? h / 2 : 1;
    const Targets* tg = EnsureTargets(hw, hh);
    if (!tg) return false;
    const TextureHandle a = tg->a, b = tg->b;
    CommandList& cl = m_list;
    cl.Reset();
    auto pass = [&](TextureHandle dst, LoadOp load, PipelineHandle pipe, TextureHandle src, const float* u) {
        RenderPassDesc rp; rp.color = dst; rp.colorLoad = load;
        cl.BeginRenderPass(rp);
        cl.SetPipeline(pipe);
        cl.SetVertexBuffer(m_quad);
        cl.SetTexture(0, src, m_linear);
        cl.SetUniforms(u, 16);
        cl.Draw(6);
        cl.EndRenderPass();
    };
    // 1) bright pass: frame -> half-size A
    const float bright[4] = {threshold, 0, 0, 0};
    pass(a, LoadOp::DontCare, m_bright, frame, bright);
    // 2) two separable gaussian rounds: A -> B (horizontal), B -> A (vertical)
    const float dirH[4] = {1.0f / (float)hw, 0, 0, 0}, dirV[4] = {0, 1.0f / (float)hh, 0, 0};
    for (int it = 0; it < 2; ++it) {
        pass(b, LoadOp::DontCare, m_blur, a, dirH);
        pass(a, LoadOp::DontCare, m_blur, b, dirV);
    }
    // 3) additive composite back onto the frame (shader alpha 0: the frame's alpha stays)
    const float add[4] = {intensity, 0, 0, 0};
    pass(frame, LoadOp::Load, m_add, a, add);
    return m_dev->Submit(cl);
}

} // namespace rhi
} // namespace okay
