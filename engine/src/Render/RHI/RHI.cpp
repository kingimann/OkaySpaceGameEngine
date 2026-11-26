// RHI common code: names and sizes, shader description checks, command list recording
// (with texture state tracking) and the device's deferred destruction. Design:
// docs/rendering/rhi.md. Written by us; see RHI.hpp for the sources.
#include "okay/Render/RHI/RHI.hpp"

#include <algorithm>
#include <cstring>

namespace okay {
namespace rhi {

const char* BackendName(Backend b) {
    switch (b) {
        case Backend::Software: return "Software";
        case Backend::OpenGL: return "OpenGL";
        case Backend::D3D11: return "Direct3D 11";
        case Backend::D3D12: return "Direct3D 12";
        case Backend::Vulkan: return "Vulkan";
        case Backend::WebGPU: return "WebGPU";
    }
    return "?";
}

bool IsDepth(Format f) { return f == Format::Depth; }

int BytesPerPixel(Format f) {
    switch (f) {
        case Format::RGBA8: return 4;
        case Format::RGBA16F: case Format::RGBA32F: return 16;
        case Format::Depth: return 4;   // one float
    }
    return 4;
}

int UniformTypeSize(UniformType t) {
    switch (t) {
        case UniformType::Float: case UniformType::Int: return 4;
        case UniformType::Vec2: return 8;
        case UniformType::Vec3: return 12;
        case UniformType::Vec4: return 16;
        case UniformType::Mat4: return 64;
    }
    return 4;
}

std::string ValidateShaderDesc(const ShaderDesc& d) {
    if (d.uniformBytes > kMaxUniformBytes) return "uniform block larger than kMaxUniformBytes";
    if ((int)d.attributes.size() > kMaxAttributes) return "too many vertex attributes";
    if ((int)d.textures.size() > kMaxTextureSlots) return "too many texture slots";
    if (d.cpuVaryings < 0 || d.cpuVaryings > kMaxVaryings) return "cpuVaryings out of range";
    std::vector<std::pair<std::uint32_t, std::uint32_t>> spans;
    for (const UniformField& f : d.uniforms) {
        const std::uint32_t sz = (std::uint32_t)UniformTypeSize(f.type);
        if (f.name.empty()) return "a uniform field has no name";
        if (f.offset % 4 != 0) return "uniform '" + f.name + "' is not 4-byte aligned";
        if (f.offset + sz > d.uniformBytes) return "uniform '" + f.name + "' runs past uniformBytes";
        if (f.type == UniformType::Mat4) {
            if (f.offset % 16 != 0) return "uniform '" + f.name + "' (mat4) must start on 16 bytes";
        } else if (f.offset / 16 != (f.offset + sz - 1) / 16) {
            return "uniform '" + f.name + "' crosses a 16-byte boundary";
        }
        for (const auto& s : spans)
            if (f.offset < s.second && s.first < f.offset + sz) return "uniform '" + f.name + "' overlaps another field";
        spans.push_back({f.offset, f.offset + sz});
    }
    return std::string();
}

std::string ValidateHlslDesc(const ShaderDesc& d) {
    if (d.hlslVertex.empty() || d.hlslFragment.empty()) return "no HLSL";
    auto has = [](const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; };
    if (!has(d.hlslVertex, "VSMain")) return "the HLSL vertex stage has no VSMain";
    if (!has(d.hlslFragment, "PSMain")) return "the HLSL pixel stage has no PSMain";
    if (!has(d.hlslVertex, "rhiClip(")) return "the HLSL vertex stage does not return rhiClip(position)";
    for (std::size_t i = 0; i < d.attributes.size(); ++i)
        if (!has(d.hlslVertex, "TEXCOORD" + std::to_string(i))) return "vertex attribute " + std::to_string(i) + " (" + d.attributes[i] + ") is not input TEXCOORD" + std::to_string(i);
    if (d.uniformBytes && !has(d.hlslVertex + d.hlslFragment, "register(b0)")) return "the uniform block is not a cbuffer at register(b0)";
    for (const UniformField& f : d.uniforms)
        if (!has(d.hlslVertex + d.hlslFragment, f.name)) return "uniform '" + f.name + "' is not in the HLSL";
    for (std::size_t i = 0; i < d.textures.size(); ++i) {
        const std::string n = std::to_string(i);
        if (!has(d.hlslFragment, "register(t" + n + ")") || !has(d.hlslFragment, "register(s" + n + ")"))
            return "texture slot " + n + " (" + d.textures[i] + ") is not at register(t" + n + ") with its sampler at register(s" + n + ")";
    }
    return "";
}

std::string ValidatePipelineDesc(const PipelineDesc& d) {
    unsigned seen = 0;
    for (const VertexAttribute& a : d.layout.attributes) {
        if (a.location < 0 || a.location >= kMaxAttributes) return "vertex attribute location out of range";
        if (seen & (1u << a.location)) return "vertex attribute location used twice";
        seen |= 1u << a.location;
        const std::uint32_t size = a.format == VertexFormat::Float1 ? 4 : a.format == VertexFormat::Float2 ? 8
                                 : a.format == VertexFormat::Float3 ? 12 : 16;
        const std::uint32_t bytes = a.format == VertexFormat::UNorm8x4 ? 4 : size;
        if (a.offset + bytes > d.layout.stride) return "vertex attribute runs past the stride";
    }
    return std::string();
}

bool Device::CheckPipeline(const PipelineDesc& d, bool shaderKnown) {
    if (!shaderKnown) { SetError("CreatePipeline: unknown shader"); return false; }
    const std::string err = ValidatePipelineDesc(d);
    if (!err.empty()) { SetError("CreatePipeline: " + err); return false; }
    return true;
}

// ---- CommandList ---------------------------------------------------------------------
// Encoding of Command fields:
//   BeginPass a=pass index | Viewport a,b,c,d = x,y,w,h | Pipeline a=id | VertexBuffer a=id b=offset
//   IndexBuffer a=id b=IndexType c=offset | Texture a=slot b=texture c=sampler | Uniforms a=arena offset b=bytes
//   Draw a=count b=first | DrawIndexed a=count b=firstIndex c=baseVertex | Transition a=texture b=ResourceState

void CommandList::Reset() {
    m_cmds.clear(); m_passes.clear(); m_arena.clear(); m_states.clear(); m_error.clear();
    m_inPass = false; m_havePipeline = false; m_cur = RenderPassDesc{};
}

void CommandList::Fail(const char* what) {
    if (m_error.empty()) m_error = what;
}

std::vector<std::pair<std::uint32_t, ResourceState>> CommandList::Transitions() const {
    std::vector<std::pair<std::uint32_t, ResourceState>> out;
    for (const Command& c : m_cmds)
        if (c.op == Op::Transition) out.push_back({c.a, (ResourceState)c.b});
    return out;
}

// Explicit APIs cannot change a texture's state inside a render pass, so a transition
// asked for inside one is placed just before the pass's BeginPass command.
void CommandList::Track(TextureHandle t, ResourceState s) {
    if (!t) return;
    bool known = false;
    for (auto& st : m_states)
        if (st.first == t.id) {
            if (st.second == s) return;
            st.second = s;
            known = true;
            break;
        }
    if (!known) m_states.push_back({t.id, s});
    Command c{Op::Transition, t.id, (std::uint32_t)s, 0, 0};
    if (m_inPass) {
        for (std::size_t i = m_cmds.size(); i-- > 0;)
            if (m_cmds[i].op == Op::BeginPass) { m_cmds.insert(m_cmds.begin() + (std::ptrdiff_t)i, c); return; }
    }
    m_cmds.push_back(c);
}

void CommandList::BeginRenderPass(const RenderPassDesc& pass) {
    if (m_inPass) { Fail("BeginRenderPass inside a render pass"); return; }
    if (!pass.color && !pass.depth) { Fail("render pass with no attachment"); return; }
    if (pass.color && pass.color == pass.depth) { Fail("one texture as both colour and depth"); return; }
    Track(pass.color, ResourceState::RenderTarget);
    Track(pass.depth, ResourceState::DepthWrite);
    m_passes.push_back(pass);
    m_cmds.push_back({Op::BeginPass, (std::uint32_t)(m_passes.size() - 1), 0, 0, 0});
    m_inPass = true;
    m_cur = pass;
}

void CommandList::EndRenderPass() {
    if (!m_inPass) { Fail("EndRenderPass without BeginRenderPass"); return; }
    m_cmds.push_back({Op::EndPass});
    m_inPass = false;
    m_havePipeline = false;   // a pipeline is bound per pass
    m_cur = RenderPassDesc{};
}

void CommandList::SetViewport(int x, int y, int w, int h) {
    if (!m_inPass) { Fail("SetViewport outside a render pass"); return; }
    if (w <= 0 || h <= 0) { Fail("SetViewport with an empty size"); return; }
    m_cmds.push_back({Op::Viewport, (std::uint32_t)x, (std::uint32_t)y, (std::uint32_t)w, (std::uint32_t)h});
}

void CommandList::SetPipeline(PipelineHandle p) {
    if (!m_inPass) { Fail("SetPipeline outside a render pass"); return; }
    if (!p) { Fail("SetPipeline with an empty handle"); return; }
    m_cmds.push_back({Op::Pipeline, p.id});
    m_havePipeline = true;
}

void CommandList::SetVertexBuffer(BufferHandle b, std::uint32_t offset) {
    if (!m_inPass) { Fail("SetVertexBuffer outside a render pass"); return; }
    if (!b) { Fail("SetVertexBuffer with an empty handle"); return; }
    m_cmds.push_back({Op::VertexBuffer, b.id, offset});
}

void CommandList::SetIndexBuffer(BufferHandle b, IndexType type, std::uint32_t offset) {
    if (!m_inPass) { Fail("SetIndexBuffer outside a render pass"); return; }
    if (!b) { Fail("SetIndexBuffer with an empty handle"); return; }
    m_cmds.push_back({Op::IndexBuffer, b.id, (std::uint32_t)type, offset});
}

void CommandList::SetTexture(int slot, TextureHandle t, SamplerHandle s) {
    if (!m_inPass) { Fail("SetTexture outside a render pass"); return; }
    if (slot < 0 || slot >= kMaxTextureSlots) { Fail("SetTexture slot out of range"); return; }
    if (t && m_inPass && (t == m_cur.color || t == m_cur.depth)) {
        Fail("SetTexture: the texture is an attachment of the current render pass (feedback loop)");
        return;
    }
    if (t && !s) { Fail("SetTexture without a sampler"); return; }
    Track(t, ResourceState::ShaderRead);
    m_cmds.push_back({Op::Texture, (std::uint32_t)slot, t.id, s.id});
}

void CommandList::SetUniforms(const void* data, std::uint32_t bytes) {
    if (!m_inPass) { Fail("SetUniforms outside a render pass"); return; }
    if (bytes > kMaxUniformBytes) { Fail("SetUniforms larger than kMaxUniformBytes"); return; }
    const std::size_t at = (m_arena.size() + 15) & ~std::size_t(15);   // 16-byte aligned blocks
    m_arena.resize(at + bytes);
    if (bytes) std::memcpy(m_arena.data() + at, data, bytes);
    m_cmds.push_back({Op::Uniforms, (std::uint32_t)at, bytes});
}

void CommandList::Draw(std::uint32_t vertexCount, std::uint32_t firstVertex) {
    if (!m_inPass) { Fail("Draw outside a render pass"); return; }
    if (!m_havePipeline) { Fail("Draw without a pipeline"); return; }
    m_cmds.push_back({Op::Draw, vertexCount, firstVertex});
}

void CommandList::DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::int32_t baseVertex) {
    if (!m_inPass) { Fail("DrawIndexed outside a render pass"); return; }
    if (!m_havePipeline) { Fail("DrawIndexed without a pipeline"); return; }
    m_cmds.push_back({Op::DrawIndexed, indexCount, firstIndex, (std::uint32_t)baseVertex});
}

// ---- Device: deferred destruction -------------------------------------------------------
// A backend that lets the CPU run ahead (framesInFlight > 1) may still be using a resource
// the caller destroys; it is freed once that many frames have ended. OpenGL and Software
// (framesInFlight 1) free at once.
void Device::Defer(Kind k, std::uint32_t id) {
    if (!id) return;
    const int fif = GetCaps().framesInFlight;
    if (fif <= 1) { Release(k, id); return; }
    m_pending.push_back({k, id, m_frame});
}

void Device::EndFrame() {
    ++m_frame;
    const int fif = GetCaps().framesInFlight;
    std::size_t keep = 0;
    for (std::size_t i = 0; i < m_pending.size(); ++i) {
        if (m_frame >= m_pending[i].frame + (std::uint64_t)(fif > 1 ? fif - 1 : 0)) Release(m_pending[i].kind, m_pending[i].id);
        else m_pending[keep++] = m_pending[i];
    }
    m_pending.resize(keep);
}

void Device::ReleaseAllDeferred() {
    for (const Pending& p : m_pending) Release(p.kind, p.id);
    m_pending.clear();
}

} // namespace rhi
} // namespace okay
