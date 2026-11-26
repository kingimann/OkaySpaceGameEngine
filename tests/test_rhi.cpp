// The RHI (rendering hardware interface, docs/rendering/rhi.md) on its Software backend, headless:
// device and capabilities, buffer and texture upload and read-back, shader layout checks, command
// list recording, validation and state transitions, the rasterization rules (every pixel of a tiled
// surface drawn exactly once, exact coverage of a triangle), blending, write masks, depth, culling,
// indexed draws, the OkayShader surface path, the bloom pass, resource lifetime, the Direct3D 11 HLSL rules
// (as text: that backend runs only on Windows) and the test scene
// against its expected picture (tests/golden/rhi/software_scene.png).
//   OKAY_RHI_WRITE_GOLDEN=1   rewrite the expected picture (then review and commit it)
//   OKAY_SHOT_DIR=<folder>    also save the scene as rhi_software.png there
#include "test_framework.hpp"
#include "rhi_test_scene.hpp"
#include "okay/Render/RHI/BloomPass.hpp"
#include "okay/Render/RHI/SurfaceShader.hpp"

#include <cstring>

using namespace okay;
using namespace okay::rhi;

namespace {

// A fullscreen pipeline whose fragment writes the uniform colour (vec4 at 0).
void PosVert(const float (*a)[4], const void*, const void*, float clip[4], float*) {
    clip[0] = a[0][0]; clip[1] = a[0][1]; clip[2] = a[0][2]; clip[3] = 1.0f;
}
bool UniformColorFrag(const float*, const FragmentContext& ctx, float out[4]) {
    std::memcpy(out, ctx.uniforms, 16);
    return true;
}
ShaderDesc UniformColorShader() {
    ShaderDesc d;
    d.debugName = "uniform colour";
    d.attributes = {"aPos"};
    d.uniforms = {UniformField{"uColor", UniformType::Vec4, 0}};
    d.uniformBytes = 16;
    d.glslVertex = "#version 120\nattribute vec3 aPos; void main(){ gl_Position = vec4(aPos, 1.0); }\n";
    d.glslFragment = "#version 120\nuniform vec4 uColor; void main(){ gl_FragColor = uColor; }\n";
    d.cpuVertex = &PosVert; d.cpuFragment = &UniformColorFrag;
    return d;
}

struct Fixture {
    std::unique_ptr<Device> dev = CreateSoftwareDevice();
    ShaderHandle sh;
    Fixture() { sh = dev->CreateShader(UniformColorShader()); }
    PipelineHandle Pipe(BlendState b = {}, DepthState d = {}, CullMode c = CullMode::None) {
        PipelineDesc pd; pd.shader = sh; pd.blend = b; pd.depth = d; pd.cull = c;
        pd.layout.stride = 12; pd.layout.attributes = {VertexAttribute{0, VertexFormat::Float3, 0}};
        return dev->CreatePipeline(pd);
    }
    BufferHandle Verts(const std::vector<float>& v) {
        BufferDesc bd; bd.size = v.size() * 4; bd.initialData = v.data(); return dev->CreateBuffer(bd);
    }
    TextureHandle Target(int w, int h, Format f = Format::RGBA8) {
        TextureDesc td; td.width = w; td.height = h; td.format = f; td.renderTarget = true; return dev->CreateTexture(td);
    }
};

std::vector<float> Quad(float x0, float y0, float x1, float y1, float z = 0.0f) {   // counter-clockwise
    return {x0, y0, z, x1, y0, z, x1, y1, z, x0, y0, z, x1, y1, z, x0, y1, z};
}

// A device that makes handles and counts them but draws nothing: for the rules every backend shares
// (deferred destruction when the CPU runs ahead; the HLSL rules the Direct3D 11 backend enforces).
struct StandInDevice : Device {
    Caps caps; std::size_t live = 0; bool checkHlsl = false; std::vector<ShaderDesc> shaders;
    const Caps& GetCaps() const override { return caps; }
    BufferHandle CreateBuffer(const BufferDesc&) override { ++live; return BufferHandle{NextId()}; }
    TextureHandle CreateTexture(const TextureDesc&) override { ++live; return TextureHandle{NextId()}; }
    SamplerHandle CreateSampler(const SamplerDesc&) override { ++live; return SamplerHandle{NextId()}; }
    ShaderHandle CreateShader(const ShaderDesc& sd) override {
        if (checkHlsl && !ValidateHlslDesc(sd).empty()) return {};
        shaders.push_back(sd); ++live; return ShaderHandle{NextId()};
    }
    PipelineHandle CreatePipeline(const PipelineDesc&) override { ++live; return PipelineHandle{NextId()}; }
    SwapChainHandle CreateSwapChain(int, int) override { return {}; }
    bool UpdateBuffer(BufferHandle, std::size_t, const void*, std::size_t) override { return false; }
    bool UpdateTexture(TextureHandle, const void*) override { return false; }
    bool ReadTexture(TextureHandle, std::vector<std::uint8_t>&) override { return false; }
    bool TextureSize(TextureHandle, int&, int&) const override { return false; }
    TextureHandle BackBuffer(SwapChainHandle) override { return {}; }
    bool ResizeSwapChain(SwapChainHandle, int, int) override { return false; }
    bool Present(SwapChainHandle) override { return false; }
    bool Submit(const CommandList& l) override { return l.Error().empty(); }
    std::size_t LiveResources() const override { return live; }
    void Release(Kind, std::uint32_t) override { --live; }
};

} // namespace

int main() {
    RUN_SUITE("rhi");

    // ---- Device and capabilities ----
    {
        auto dev = CreateSoftwareDevice();
        CHECK(dev != nullptr);
        const Caps& c = dev->GetCaps();
        CHECK(c.backend == Backend::Software);
        CHECK(std::string(BackendName(c.backend)) == "Software");
        CHECK(c.maxTextureSize >= 4096);
        CHECK(c.floatRenderTargets && c.depthTextures && c.indexU32 && c.drawBaseVertex);
        CHECK(!c.needsBarriers && c.framesInFlight == 1);
        CHECK(dev->LiveResources() == 0);
    }

    // ---- Buffers and textures: upload and read back ----
    {
        auto dev = CreateSoftwareDevice();
        std::uint8_t px[3 * 2 * 4];
        for (int i = 0; i < 24; ++i) px[i] = (std::uint8_t)(i * 10 + 3);
        TextureDesc td; td.width = 3; td.height = 2; td.initialData = px;
        TextureHandle t = dev->CreateTexture(td);
        std::vector<std::uint8_t> back;
        CHECK(t && dev->ReadTexture(t, back) && back.size() == 24 && std::memcmp(back.data(), px, 24) == 0);
        int w = 0, h = 0; CHECK(dev->TextureSize(t, w, h) && w == 3 && h == 2);
        px[5] = 99; CHECK(dev->UpdateTexture(t, px) && dev->ReadTexture(t, back) && back[5] == 99);
        // RGBA16F keeps half-precision values: 1/3 becomes 0.333251953125; RGBA32F keeps it exactly.
        const float f[4] = {1.0f / 3.0f, 1000.7f, -2.5f, 1e-6f};
        td = TextureDesc{}; td.width = 1; td.height = 1; td.format = Format::RGBA16F; td.initialData = f;
        TextureHandle th = dev->CreateTexture(td);
        float r[4];
        CHECK(dev->ReadTexture(th, back) && back.size() == 16);
        std::memcpy(r, back.data(), 16);
        CHECK(r[0] == 0.333251953125f && r[1] == 1000.5f && r[2] == -2.5f);
        CHECK(std::fabs(r[3] - 1e-6f) < 6e-8f && r[3] != 1e-6f);   // a subnormal half
        td.format = Format::RGBA32F; TextureHandle tf = dev->CreateTexture(td);
        CHECK(dev->ReadTexture(tf, back)); std::memcpy(r, back.data(), 16); CHECK(r[0] == 1.0f / 3.0f);
        const std::uint16_t idx[4] = {1, 2, 3, 4};
        BufferDesc bd; bd.usage = BufferUsage::Index; bd.size = 8; bd.initialData = idx;
        BufferHandle b = dev->CreateBuffer(bd);
        CHECK(b && dev->UpdateBuffer(b, 2, idx, 4) && !dev->UpdateBuffer(b, 6, idx, 4));
        bd.size = 0; CHECK(!dev->CreateBuffer(bd));
        td.width = 0; CHECK(!dev->CreateTexture(td));
        CHECK(dev->LiveResources() == 4);
        dev->Destroy(t); dev->Destroy(th); dev->Destroy(tf); dev->Destroy(b);
        CHECK(dev->LiveResources() == 0);
    }

    // ---- Shader descriptions: layout rules every backend shares ----
    {
        ShaderDesc d = UniformColorShader();
        CHECK(ValidateShaderDesc(d).empty());
        d.uniforms = {UniformField{"a", UniformType::Vec3, 8}}; d.uniformBytes = 32;
        CHECK(ValidateShaderDesc(d).find("16-byte") != std::string::npos);
        d.uniforms = {UniformField{"m", UniformType::Mat4, 4}}; d.uniformBytes = 80;
        CHECK(!ValidateShaderDesc(d).empty());
        d.uniforms = {UniformField{"a", UniformType::Vec4, 0}, UniformField{"b", UniformType::Float, 12}}; d.uniformBytes = 16;
        CHECK(ValidateShaderDesc(d).find("overlaps") != std::string::npos);
        d.uniforms = {UniformField{"a", UniformType::Vec4, 16}}; d.uniformBytes = 16;
        CHECK(ValidateShaderDesc(d).find("past") != std::string::npos);
        auto dev = CreateSoftwareDevice();
        ShaderDesc noCpu = UniformColorShader(); noCpu.cpuFragment = nullptr;
        CHECK(!dev->CreateShader(noCpu) && !dev->LastError().empty());
        // Pipeline layouts: attributes inside the stride, each location once.
        ShaderHandle ok = dev->CreateShader(UniformColorShader());
        PipelineDesc pd; pd.shader = ok; pd.layout.stride = 12; pd.layout.attributes = {VertexAttribute{0, VertexFormat::Float3, 0}};
        CHECK(ValidatePipelineDesc(pd).empty() && dev->CreatePipeline(pd));
        pd.layout.attributes = {VertexAttribute{0, VertexFormat::Float3, 4}};
        CHECK(!ValidatePipelineDesc(pd).empty() && !dev->CreatePipeline(pd));
        pd.layout.stride = 16; pd.layout.attributes = {VertexAttribute{1, VertexFormat::Float2, 0}, VertexAttribute{1, VertexFormat::UNorm8x4, 8}};
        CHECK(ValidatePipelineDesc(pd).find("twice") != std::string::npos);
        pd.layout.attributes = {VertexAttribute{kMaxAttributes, VertexFormat::Float1, 0}};
        CHECK(!ValidatePipelineDesc(pd).empty());
        pd.layout.attributes.clear(); pd.shader = ShaderHandle{12345};
        CHECK(!dev->CreatePipeline(pd));   // unknown shader
    }

    // ---- Command lists: validation, uniform memory and state transitions ----
    {
        Fixture fx;
        TextureHandle a = fx.Target(8, 8), b = fx.Target(8, 8);
        PipelineHandle p = fx.Pipe();
        SamplerHandle s = fx.dev->CreateSampler(SamplerDesc{});
        CommandList cl;
        cl.Draw(3); CHECK(cl.Error().find("outside") != std::string::npos);
        CHECK(!fx.dev->Submit(cl));
        cl.Reset(); RenderPassDesc rp; rp.color = a; cl.BeginRenderPass(rp); cl.Draw(3);
        CHECK(cl.Error().find("pipeline") != std::string::npos);
        cl.Reset(); cl.BeginRenderPass(rp); cl.SetTexture(0, a, s);
        CHECK(cl.Error().find("feedback") != std::string::npos);
        cl.Reset(); cl.BeginRenderPass(rp); cl.BeginRenderPass(rp); CHECK(!cl.Error().empty());
        cl.Reset(); cl.EndRenderPass(); CHECK(!cl.Error().empty());
        // Draw into A, then read A while drawing B: A goes RenderTarget -> ShaderRead, and that
        // transition is placed before the second pass begins (explicit APIs cannot change states inside a pass).
        cl.Reset();
        const float red[4] = {1, 0, 0, 1};
        cl.BeginRenderPass(rp); cl.SetPipeline(p); cl.SetUniforms(red, 16); cl.EndRenderPass();
        RenderPassDesc rp2; rp2.color = b;
        cl.BeginRenderPass(rp2); cl.SetPipeline(p); cl.SetTexture(0, a, s); cl.SetUniforms(red, 3); cl.EndRenderPass();
        CHECK(cl.Error().empty());
        const auto tr = cl.Transitions();
        CHECK(tr.size() == 3);
        if (tr.size() == 3) {
            CHECK(tr[0].first == a.id && tr[0].second == ResourceState::RenderTarget);
            CHECK(tr[1].first == b.id && tr[1].second == ResourceState::RenderTarget);
            CHECK(tr[2].first == a.id && tr[2].second == ResourceState::ShaderRead);
        }
        const auto& cmds = cl.Commands();
        std::size_t secondBegin = 0, lastTransition = 0; int begins = 0;
        for (std::size_t i = 0; i < cmds.size(); ++i) {
            if (cmds[i].op == CommandList::Op::BeginPass && ++begins == 2) secondBegin = i;
            if (cmds[i].op == CommandList::Op::Transition) lastTransition = i;
        }
        CHECK(lastTransition < secondBegin);
        CHECK(cl.UniformBytesUsed() == 16 + 3);   // two blocks, the second 16-byte aligned
        CHECK(fx.dev->Submit(cl));
        CHECK(fx.dev->GetStats().transitions == 3 && fx.dev->GetStats().passes == 2);
    }

    // ---- Rasterization rules ----
    {
        // A surface tiled by many triangles (and a full-screen quad), drawn with additive blending:
        // every pixel must be drawn exactly once (no gaps, nothing counted twice on shared edges).
        Fixture fx;
        const int W = 37, H = 23;
        TextureHandle t = fx.Target(W, H);
        PipelineHandle add = fx.Pipe(BlendState::Additive());
        std::vector<float> grid;
        const int NX = 7, NY = 5;
        for (int j = 0; j < NY; ++j)
            for (int i = 0; i < NX; ++i) {
                auto q = Quad(-1 + 2.0f * i / NX, -1 + 2.0f * j / NY, -1 + 2.0f * (i + 1) / NX, -1 + 2.0f * (j + 1) / NY);
                if ((i + j) & 1) { std::swap(q[3], q[6]); std::swap(q[4], q[7]); std::swap(q[12], q[15]); std::swap(q[13], q[16]); }   // mixed windings
                grid.insert(grid.end(), q.begin(), q.end());
            }
        BufferHandle vb = fx.Verts(grid), full = fx.Verts(Quad(-1, -1, 1, 1));
        const float one[4] = {10 / 255.0f, 20 / 255.0f, 30 / 255.0f, 40 / 255.0f};
        CommandList cl; RenderPassDesc rp; rp.color = t; rp.colorLoad = LoadOp::Clear;
        cl.BeginRenderPass(rp); cl.SetPipeline(add); cl.SetUniforms(one, 16);
        cl.SetVertexBuffer(vb); cl.Draw((std::uint32_t)grid.size() / 3);
        cl.SetVertexBuffer(full); cl.Draw(6);
        cl.EndRenderPass();
        std::vector<std::uint8_t> px;
        CHECK(fx.dev->Submit(cl) && fx.dev->ReadTexture(t, px));
        int bad = 0;
        for (int i = 0; i < W * H; ++i)
            if (px[i * 4] != 20 || px[i * 4 + 1] != 40 || px[i * 4 + 2] != 60 || px[i * 4 + 3] != 80) ++bad;
        CHECK(bad == 0);
        if (bad) std::printf("  tiled surface: %d pixels not drawn exactly once\n", bad);

        // Exact coverage of one triangle: a pixel is drawn when its centre is inside.
        const int S = 41;
        TextureHandle t2 = fx.Target(S, S);
        PipelineHandle opaque = fx.Pipe();
        BufferHandle tri = fx.Verts({-1, -1, 0, 1, -1, 0, -1, 1, 0});   // window (0,0) (S,0) (0,S)
        const float white[4] = {1, 1, 1, 1};
        cl.Reset(); rp.color = t2; cl.BeginRenderPass(rp); cl.SetPipeline(opaque); cl.SetVertexBuffer(tri);
        cl.SetUniforms(white, 16); cl.Draw(3); cl.EndRenderPass();
        CHECK(fx.dev->Submit(cl) && fx.dev->ReadTexture(t2, px));
        int wrong = 0;
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x) {
                // < 0 inside. 0 is a centre exactly on the long edge: that edge is neither top nor left, so not drawn.
                const double e = (x + 0.5) + (y + 0.5) - S;
                if ((px[(y * S + x) * 4] == 255) != (e < 0)) ++wrong;
            }
        CHECK(wrong == 0);
    }

    // ---- Blending, write mask, depth and culling ----
    {
        Fixture fx;
        TextureHandle t = fx.Target(4, 4);
        BufferHandle full = fx.Verts(Quad(-1, -1, 1, 1));
        auto run = [&](PipelineHandle p, const float* col, std::vector<std::uint8_t>& out) {
            CommandList cl; RenderPassDesc rp; rp.color = t; rp.colorLoad = LoadOp::Clear;
            rp.clearColor[0] = 0.2f; rp.clearColor[1] = 0.4f; rp.clearColor[2] = 0.6f; rp.clearColor[3] = 1.0f;
            cl.BeginRenderPass(rp); cl.SetPipeline(p); cl.SetVertexBuffer(full); cl.SetUniforms(col, 16); cl.Draw(6); cl.EndRenderPass();
            return fx.dev->Submit(cl) && fx.dev->ReadTexture(t, out);
        };
        std::vector<std::uint8_t> px;
        const float src[4] = {1.0f, 0.0f, 0.25f, 0.5f};
        CHECK(run(fx.Pipe(BlendState::Alpha()), src, px));
        const std::uint8_t clear[4] = {51, 102, 153, 255};   // the clear colour as stored
        for (int k = 0; k < 4; ++k) {
            const float d = clear[k] / 255.0f;
            const float e = src[k] * src[3] + d * (1.0f - src[3]);
            CHECK(px[k] == (std::uint8_t)std::lrint(std::fmin(e, 1.0f) * 255.0f));
        }
        BlendState mask; mask.writeMask = 0x1;   // red only
        CHECK(run(fx.Pipe(mask), src, px));
        CHECK(px[0] == 255 && px[1] == clear[1] && px[2] == clear[2] && px[3] == clear[3]);

        // Depth: two overlapping quads give the same picture in either order with a Less test.
        TextureHandle c = fx.Target(16, 16);
        TextureDesc dd; dd.width = 16; dd.height = 16; dd.format = Format::Depth; dd.renderTarget = true;
        TextureHandle depth = fx.dev->CreateTexture(dd);
        DepthState ds; ds.test = true; ds.write = true; ds.compare = CompareOp::Less;
        PipelineHandle pd = fx.Pipe({}, ds);
        BufferHandle nearQ = fx.Verts(Quad(-1, -1, 0.5f, 0.5f, -0.5f)), farQ = fx.Verts(Quad(-0.5f, -0.5f, 1, 1, 0.5f));
        const float red[4] = {1, 0, 0, 1}, blue[4] = {0, 0, 1, 1};
        auto twoQuads = [&](bool nearFirst, std::vector<std::uint8_t>& out) {
            CommandList cl; RenderPassDesc rp; rp.color = c; rp.depth = depth; rp.colorLoad = LoadOp::Clear;
            cl.BeginRenderPass(rp); cl.SetPipeline(pd);
            for (int i = 0; i < 2; ++i) {
                const bool n = (i == 0) == nearFirst;
                cl.SetVertexBuffer(n ? nearQ : farQ); cl.SetUniforms(n ? red : blue, 16); cl.Draw(6);
            }
            cl.EndRenderPass();
            return fx.dev->Submit(cl) && fx.dev->ReadTexture(c, out);
        };
        std::vector<std::uint8_t> a, b;
        CHECK(twoQuads(true, a) && twoQuads(false, b) && a == b);
        const std::size_t mid = ((std::size_t)8 * 16 + 8) * 4;   // inside both quads: the near one (red) wins
        CHECK(a[mid] == 255 && a[mid + 2] == 0);

        // Culling: the counter-clockwise quad is a front face.
        PipelineHandle cullBack = fx.Pipe({}, {}, CullMode::Back), cullFront = fx.Pipe({}, {}, CullMode::Front);
        CHECK(run(cullBack, red, px) && px[0] == 255 && px[1] == 0);
        CHECK(run(cullFront, red, px) && px[0] == clear[0] && px[1] == clear[1]);
    }

    // ---- Indexed draws (U16 and U32) equal the expanded draw; a bad index is refused ----
    {
        Fixture fx;
        std::vector<rhitest::ColorVertex> v; std::vector<std::uint16_t> i16;
        rhitest::BuildCube(v, i16);
        std::vector<std::uint32_t> i32(i16.begin(), i16.end());
        std::vector<rhitest::ColorVertex> expanded;
        for (std::uint16_t i : i16) expanded.push_back(v[i]);
        ShaderHandle cs = fx.dev->CreateShader(rhitest::ColorShader());
        PipelineDesc pd; pd.shader = cs; pd.cull = CullMode::Back;
        pd.depth.test = pd.depth.write = true;
        pd.layout.stride = sizeof(rhitest::ColorVertex);
        pd.layout.attributes = {VertexAttribute{0, VertexFormat::Float3, 0}, VertexAttribute{1, VertexFormat::UNorm8x4, 12}};
        PipelineHandle p = fx.dev->CreatePipeline(pd);
        BufferDesc bd; bd.size = v.size() * sizeof(v[0]); bd.initialData = v.data();
        BufferHandle vb = fx.dev->CreateBuffer(bd);
        bd.size = expanded.size() * sizeof(v[0]); bd.initialData = expanded.data();
        BufferHandle xb = fx.dev->CreateBuffer(bd);
        bd.usage = BufferUsage::Index; bd.size = i16.size() * 2; bd.initialData = i16.data();
        BufferHandle ib16 = fx.dev->CreateBuffer(bd);
        bd.size = i32.size() * 4; bd.initialData = i32.data();
        BufferHandle ib32 = fx.dev->CreateBuffer(bd);
        TextureHandle c = fx.Target(48, 48);
        TextureDesc dd; dd.width = 48; dd.height = 48; dd.format = Format::Depth; dd.renderTarget = true;
        TextureHandle depth = fx.dev->CreateTexture(dd);
        const Mat4 mvp = Mat4::Perspective(60.0f, 1.0f, 0.1f, 10.0f) * Mat4::LookAt(Vec3{2.5f, 2.0f, 3.0f}, Vec3{0, 0, 0}, Vec3{0, 1, 0});
        auto draw = [&](int mode, std::vector<std::uint8_t>& out) {
            CommandList cl; RenderPassDesc rp; rp.color = c; rp.depth = depth; rp.colorLoad = LoadOp::Clear;
            cl.BeginRenderPass(rp); cl.SetPipeline(p); cl.SetUniforms(mvp.m, 64);
            if (mode == 0) { cl.SetVertexBuffer(xb); cl.Draw((std::uint32_t)expanded.size()); }
            else { cl.SetVertexBuffer(vb); cl.SetIndexBuffer(mode == 1 ? ib16 : ib32, mode == 1 ? IndexType::U16 : IndexType::U32);
                   cl.DrawIndexed((std::uint32_t)i16.size() + (mode == 3 ? 3u : 0u)); }
            cl.EndRenderPass();
            return fx.dev->Submit(cl) && fx.dev->ReadTexture(c, out);
        };
        std::vector<std::uint8_t> e, a, b;
        CHECK(draw(0, e) && draw(1, a) && draw(2, b));
        CHECK(a == e && b == e);
        int lit = 0; for (std::size_t k = 0; k < e.size(); k += 4) if (e[k + 3] == 255) ++lit;
        CHECK(lit > 400);   // the cube covers a good part of the picture
        CHECK(!draw(3, a) && fx.dev->LastError().find("index") != std::string::npos);   // reads past the index buffer
    }

    // ---- The test scene against its expected picture ----
    {
        auto dev = CreateSoftwareDevice();
        const int W = 128, H = 128;
        std::vector<std::uint8_t> px; std::string err;
        CHECK(rhitest::DrawTestScene(*dev, W, H, px, &err));
        if (!err.empty()) std::printf("  scene: %s\n", err.c_str());
        CHECK(dev->LiveResources() == 0);   // DrawTestScene destroys all it made
        const std::string golden = std::string(OKAY_SOURCE_DIR) + "/tests/golden/rhi/software_scene.png";
        if (std::getenv("OKAY_RHI_WRITE_GOLDEN")) CHECK(rhitest::SaveUpright(golden, px, W, H));
        std::vector<std::uint8_t> ref; int gw = 0, gh = 0;
        CHECK(rhitest::LoadUpright(golden, ref, gw, gh) && gw == W && gh == H);
        const rhitest::Diff d = rhitest::Compare(px, ref, 0);
        std::printf("  scene vs golden: max %d, differing pixels %d\n", d.maxDiff, d.over);
        CHECK(d.over == 0);
        if (const char* dir = std::getenv("OKAY_SHOT_DIR")) rhitest::SaveUpright(std::string(dir) + "/rhi_software.png", px, W, H);
    }

    // ---- OkayShader surfaces become RHI shaders ----
    {
        oshader::Compiled c = oshader::Compile("mode unlit; property float k = 0.5; surface { albedo = vec3(uv, k); }");
        CHECK(c.ok);
        ShaderDesc sd; std::string err;
        CHECK(ShaderFromSurface(c, sd, &err));
        CHECK(sd.glslFragment.find("okaySurface") != std::string::npos && sd.uniformBytes == kSurfacePropsOffset + 16);
        oshader::Compiled lit = oshader::Compile("surface { albedo = vec3(1.0); }");
        CHECK(lit.ok && !ShaderFromSurface(lit, sd, &err) && err.find("lit") != std::string::npos);
        CHECK(ShaderFromSurface(c, sd, &err));
        auto dev = CreateSoftwareDevice();
        const Mat4 I; const float eye[3] = {0, 0, 3};
        const std::vector<std::uint8_t> u = SurfaceUniforms(I.m, I.m, 0.0f, eye, oshader::ResolveProperties(c, "k=0.25"));
        std::vector<std::uint8_t> px;
        CHECK(rhitest::DrawSurfaceQuad(*dev, sd, u, 16, 16, px));
        CHECK(dev->LiveResources() == 0);
        int worst = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const std::uint8_t* c4 = &px[(y * 16 + x) * 4];
                const int er = std::abs(c4[0] - (int)std::lrint((x + 0.5f) / 16 * 255));
                const int eg = std::abs(c4[1] - (int)std::lrint((y + 0.5f) / 16 * 255));
                const int eb = std::abs(c4[2] - 64);
                worst = std::max(worst, std::max(er, std::max(eg, eb)));
            }
        CHECK(worst <= 1);
    }

    // ---- The bloom pass (the first feature on the RHI) ----
    {
        auto dev = CreateSoftwareDevice();
        BloomPass bloom;
        CHECK(bloom.Init(*dev));
        const int W = 32, H = 32;
        std::vector<std::uint8_t> frame((std::size_t)W * H * 4, 0);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                std::uint8_t* p = &frame[((std::size_t)y * W + x) * 4];
                const bool block = x >= 14 && x < 18 && y >= 14 && y < 18;
                p[0] = block ? 255 : 20; p[1] = block ? 250 : 30; p[2] = block ? 240 : 40; p[3] = block ? 255 : (x < 4 ? 0 : 255);
            }
        TextureDesc td; td.width = W; td.height = H; td.renderTarget = true; td.initialData = frame.data();
        TextureHandle t = dev->CreateTexture(td);
        std::vector<std::uint8_t> out;
        CHECK(bloom.Apply(t, W, H, 1.0f, 1.0f) && dev->ReadTexture(t, out));
        CHECK(out == frame);   // threshold 1: nothing is over it, the frame is unchanged
        CHECK(dev->UpdateTexture(t, frame.data()) && bloom.Apply(t, W, H, 0.5f, 1.0f) && dev->ReadTexture(t, out));
        auto at = [&](const std::vector<std::uint8_t>& v, int x, int y, int k) { return (int)v[((std::size_t)y * W + x) * 4 + k]; };
        CHECK(at(out, 13, 15, 0) > at(frame, 13, 15, 0) + 6);   // the glow spreads past the block (20 -> 32 here)
        CHECK(at(out, 20, 16, 1) > at(frame, 20, 16, 1));
        CHECK(std::abs(at(out, 11, 16, 0) - at(out, 20, 16, 0)) <= 2);   // and is symmetric (half-res texel grid: 11 <-> 20)
        bool alphaKept = true;
        for (std::size_t i = 3; i < out.size(); i += 4) if (out[i] != frame[i]) alphaKept = false;
        CHECK(alphaKept);   // additive with shader alpha 0: the host's sky behind empty pixels stays
        const std::size_t before = dev->LiveResources();
        CHECK(before == 1 + 3 + 3 + 1 + 1 + 2);   // frame + shaders + pipelines + quad + sampler + two targets
        CHECK(bloom.TargetSizes() == 1);
        // Frames of other sizes (other editor views) keep their own targets, up to kMaxSizes.
        for (int k = 0; k < 6; ++k) {
            TextureDesc o; o.width = 8 + 2 * k; o.height = 8; o.renderTarget = true;
            TextureHandle ot = dev->CreateTexture(o);
            CHECK(bloom.Apply(ot, o.width, o.height, 0.5f, 1.0f));
            dev->Destroy(ot);
        }
        CHECK(bloom.TargetSizes() == BloomPass::kMaxSizes && dev->LiveResources() == before - 2 + 2 * BloomPass::kMaxSizes);
        bloom.Shutdown();
        CHECK(dev->LiveResources() == 1);
        if (const char* dir = std::getenv("OKAY_SHOT_DIR")) rhitest::SaveUpright(std::string(dir) + "/rhi_bloom_software.png", out, W, H);
    }

    // ---- Swap chain ----
    {
        auto dev = CreateSoftwareDevice();
        SwapChainHandle sc = dev->CreateSwapChain(8, 6);
        TextureHandle bb = dev->BackBuffer(sc);
        CHECK(sc && bb);
        CommandList cl; RenderPassDesc rp; rp.color = bb; rp.colorLoad = LoadOp::Clear; rp.clearColor[1] = 1.0f; rp.clearColor[3] = 1.0f;
        cl.BeginRenderPass(rp); cl.EndRenderPass();
        std::vector<std::uint8_t> px;
        CHECK(dev->Submit(cl) && dev->Present(sc) && dev->ReadTexture(bb, px) && px.size() == 8 * 6 * 4 && px[1] == 255);
        CHECK(dev->ResizeSwapChain(sc, 5, 5)); int w, h; CHECK(dev->TextureSize(bb, w, h) && w == 5 && h == 5);
        dev->Destroy(bb); CHECK(dev->LiveResources() == 2);   // a back buffer is not destroyed on its own
        dev->Destroy(sc); CHECK(dev->LiveResources() == 0);
    }

    // ---- Resource lifetime: destroyed handles are refused; deferred destruction for devices that run ahead ----
    {
        Fixture fx;
        TextureHandle t = fx.Target(4, 4);
        PipelineHandle p = fx.Pipe();
        BufferHandle vb = fx.Verts(Quad(-1, -1, 1, 1));
        fx.dev->Destroy(vb);
        const float c[4] = {1, 1, 1, 1};
        CommandList cl; RenderPassDesc rp; rp.color = t;
        cl.BeginRenderPass(rp); cl.SetPipeline(p); cl.SetVertexBuffer(vb); cl.SetUniforms(c, 16); cl.Draw(6); cl.EndRenderPass();
        CHECK(!fx.dev->Submit(cl) && fx.dev->LastError().find("vertex buffer") != std::string::npos);
        BufferHandle vb2 = fx.Verts(Quad(-1, -1, 1, 1));
        CHECK(vb2.id != vb.id);   // ids are never reused

        // A device that lets the CPU run 3 frames ahead keeps a destroyed resource for 2 more frames.
        StandInDevice ahead;
        ahead.caps.framesInFlight = 3;
        BufferHandle b = ahead.CreateBuffer(BufferDesc{});
        ahead.Destroy(b);
        CHECK(ahead.LiveResources() == 1);
        ahead.EndFrame(); CHECK(ahead.LiveResources() == 1);
        ahead.EndFrame(); CHECK(ahead.LiveResources() == 0);
    }

    // ---- Direct3D 11: the HLSL rules, checked as text (the backend itself runs only on Windows) ----
    {
        // No D3D11 device without one to share; wrapping a D3D11 texture on another backend gives nothing.
        CHECK(CreateD3D11Device(nullptr) == nullptr);
        auto sw = CreateSoftwareDevice();
        TextureDesc td; td.width = 2; td.height = 2;
        int fake = 0;
        CHECK(!WrapD3D11Texture(*sw, &fake, td));
        // rhiClip flips y (Direct3D puts NDC +1 at row 0) and maps depth -w..w to 0..w.
        const std::string pre = kHlslPrelude;
        CHECK(pre.find("float4 rhiClip(float4 p)") != std::string::npos);
        CHECK(pre.find("-p.y") != std::string::npos && pre.find("(p.z + p.w) * 0.5") != std::string::npos);

        ShaderDesc d = UniformColorShader();
        CHECK(ValidateHlslDesc(d) == "no HLSL");
        d.hlslVertex = "float4 VSMain(float3 aPos : TEXCOORD0) : SV_Position { return rhiClip(float4(aPos, 1.0)); }\n";
        d.hlslFragment = "cbuffer U : register(b0) { float4 uColor; };\nfloat4 PSMain() : SV_Target { return uColor; }\n";   // dup-allow: test input for ValidateHlslDesc
        CHECK(ValidateHlslDesc(d).empty());
        ShaderDesc e = d; e.hlslVertex = "float4 VSMain(float3 aPos : TEXCOORD0) : SV_Position { return float4(aPos, 1.0); }\n";
        CHECK(ValidateHlslDesc(e).find("rhiClip") != std::string::npos);
        e = d; e.hlslVertex = "float4 VSMain(float3 aPos : POSITION) : SV_Position { return rhiClip(float4(aPos, 1.0)); }\n";
        CHECK(ValidateHlslDesc(e).find("TEXCOORD0") != std::string::npos);
        e = d; e.hlslFragment = "cbuffer U : register(b1) { float4 uColor; };\nfloat4 PSMain() : SV_Target { return uColor; }\n";
        CHECK(ValidateHlslDesc(e).find("b0") != std::string::npos);
        e = d; e.textures = {"uTex"};
        CHECK(ValidateHlslDesc(e).find("register(t0)") != std::string::npos);
        e = d; e.uniforms[0].name = "uTint";
        CHECK(ValidateHlslDesc(e).find("uTint") != std::string::npos);

        // Every shader the bloom pass makes carries HLSL that keeps the rules (a stand-in device records them).
        StandInDevice rec;   // refuses a shader whose HLSL breaks the rules, keeps the ones it takes
        rec.caps.backend = Backend::D3D11;
        rec.checkHlsl = true;
        {
            BloomPass bloom;
            CHECK(bloom.Init(rec));
            CHECK(rec.shaders.size() == 3);
            for (const ShaderDesc& sd : rec.shaders) {
                CHECK(ValidateHlslDesc(sd).empty());
                // The pixel stages are the GLSL ones line for line: the same constants in both.
                for (const char* k : {"0.227027", "0.316216", "1.384615", "0.2126", "uStrength"})
                    CHECK((sd.glslFragment.find(k) == std::string::npos) == (sd.hlslFragment.find(k) == std::string::npos));
            }
            const TextureHandle frame{999};
            CHECK(bloom.Apply(frame, 64, 32, 0.5f, 1.0f));
        }
        CHECK(rec.LiveResources() == 0);
    }

    TEST_MAIN_RESULT();
}
