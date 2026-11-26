// RHI OpenGL backend. It uses the OpenGL renderer's loaded entry points (gl_render::g,
// filled by GLRenderer::LoadGL), so there is one GL loader in the engine.
//
// Mapping (OpenGL 2.1 with framebuffer objects; the hosts make a 3.2 compatibility context):
//   Buffer -> buffer object; Texture -> 2D texture (sampler state is applied to the texture
//   when it is bound, GL 2.1 has no sampler objects); Shader -> program, GLSL 1.20, attribute
//   locations bound by name, the uniform block set field by field with glUniform*;
//   Pipeline -> the state applied on SetPipeline; render pass -> a framebuffer object per
//   (colour, depth) pair, cached; a swap chain's back buffer -> framebuffer 0.
// Submit leaves GL as it found the parts the OpenGL renderer relies on: no program, framebuffer
// 0, blending, depth test and culling off, masks on, texture unit 0 active with nothing bound
// on the units it used, and the vertex array object that was bound before. The clear colour is
// not restored. GL errors are left for the caller to read (GLRenderer checks once per frame).
// Written by us from the OpenGL 2.1 and 3.3 specifications.
#include "okay/Render/RHI/RHI.hpp"
#include "okay/Core/Log.hpp"

#include "../GLRendererInternal.hpp"

#include <cstring>
#include <map>
#include <unordered_map>
#include <utility>

#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif

namespace okay {
namespace rhi {

namespace {
using gl_render::g;

struct GLBuf { GLuint id = 0; GLenum target = GL_ARRAY_BUFFER; std::size_t size = 0; };
struct GLTex {
    GLuint id = 0; int w = 0, h = 0; Format fmt = Format::RGBA8;
    bool rt = false, sampled = true, owned = true;
    std::uint32_t swapChain = 0;   // back buffer of this swap chain: framebuffer 0
    SamplerDesc applied; bool appliedValid = false;
};
struct GLShader { GLuint prog = 0; std::vector<UniformField> fields; std::vector<GLint> loc; };
struct GLPipe { PipelineDesc desc; };

void FormatOf(Format f, GLint& internal, GLenum& format, GLenum& type) {
    switch (f) {
        case Format::RGBA8: internal = GL_RGBA8; format = GL_RGBA; type = GL_UNSIGNED_BYTE; return;
        case Format::RGBA16F: internal = GL_RGBA16F; format = GL_RGBA; type = GL_FLOAT; return;
        case Format::RGBA32F: internal = GL_RGBA32F; format = GL_RGBA; type = GL_FLOAT; return;
        case Format::Depth: internal = GL_DEPTH_COMPONENT24; format = GL_DEPTH_COMPONENT; type = GL_FLOAT; return;
    }
}

GLenum BlendGL(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero: return GL_ZERO;
        case BlendFactor::One: return GL_ONE;
        case BlendFactor::SrcColor: return GL_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor: return GL_DST_COLOR;
        case BlendFactor::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha: return GL_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
    }
    return GL_ONE;
}

GLenum CompareGL(CompareOp c) {
    switch (c) {
        case CompareOp::Never: return GL_NEVER;
        case CompareOp::Less: return GL_LESS;
        case CompareOp::Equal: return GL_EQUAL;
        case CompareOp::LessEqual: return GL_LEQUAL;
        case CompareOp::Greater: return GL_GREATER;
        case CompareOp::NotEqual: return GL_NOTEQUAL;
        case CompareOp::GreaterEqual: return GL_GEQUAL;
        case CompareOp::Always: return GL_ALWAYS;
    }
    return GL_LEQUAL;
}

class GLDevice final : public Device {
public:
    explicit GLDevice(const GLDeviceOptions& opt) : m_opt(opt) {
        m_caps.backend = Backend::OpenGL;
        m_caps.deviceName = "OpenGL";
        if (g.GetString) {
            if (const GLubyte* r = g.GetString(GL_RENDERER)) m_caps.deviceName = std::string("OpenGL: ") + (const char*)r;
            if (const GLubyte* v = g.GetString(GL_VERSION)) {
                const char* s = (const char*)v;
                if (std::strncmp(s, "OpenGL ES ", 10) == 0) s += 10;
                m_caps.floatRenderTargets = s[0] >= '3' && s[0] <= '9';
            }
        }
        GLint maxTex = 2048;
        if (g.GetIntegerv) g.GetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        m_caps.maxTextureSize = (int)maxTex;
        m_caps.depthTextures = true;
        m_caps.drawBaseVertex = false;   // glDrawElementsBaseVertex (GL 3.2) is not loaded yet
        m_caps.needsBarriers = false;
        m_caps.framesInFlight = 1;
        if (g.GenVertexArrays && g.BindVertexArray) g.GenVertexArrays(1, &m_vao);
    }
    ~GLDevice() override {
        if (m_abandoned) return;
        ReleaseAllDeferred();
        std::vector<std::pair<Kind, std::uint32_t>> all;
        for (auto& kv : m_pipes) all.push_back({Kind::Pipeline, kv.first});
        for (auto& kv : m_shaders) all.push_back({Kind::Shader, kv.first});
        for (auto& kv : m_buffers) all.push_back({Kind::Buffer, kv.first});
        for (auto& kv : m_swap) all.push_back({Kind::SwapChain, kv.first});
        for (auto& kv : m_textures) all.push_back({Kind::Texture, kv.first});
        for (auto& p : all) Release(p.first, p.second);
        for (auto& kv : m_fbos) g.DeleteFramebuffers(1, &kv.second);
        if (m_vao && g.DeleteVertexArrays) g.DeleteVertexArrays(1, &m_vao);
    }
    void Abandon() { m_abandoned = true; }

    const Caps& GetCaps() const override { return m_caps; }

    BufferHandle CreateBuffer(const BufferDesc& d) override {
        if (d.size == 0) { SetError("CreateBuffer: size 0"); return {}; }
        GLBuf b; b.size = d.size; b.target = d.usage == BufferUsage::Index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
        g.GenBuffers(1, &b.id);
        if (!b.id) { SetError("CreateBuffer: glGenBuffers failed"); return {}; }
        // Index buffers are filled through GL_ARRAY_BUFFER too, so the element binding of whatever
        // vertex array object is bound is never touched outside Submit.
        g.BindBuffer(GL_ARRAY_BUFFER, b.id);
        g.BufferData(GL_ARRAY_BUFFER, (GLsizeiptrOK)d.size, d.initialData, d.dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        g.BindBuffer(GL_ARRAY_BUFFER, 0);
        BufferHandle h{NextId()}; m_buffers[h.id] = b; return h;
    }
    TextureHandle CreateTexture(const TextureDesc& d) override {
        if (d.width <= 0 || d.height <= 0 || d.width > m_caps.maxTextureSize || d.height > m_caps.maxTextureSize) {
            SetError("CreateTexture: bad size"); return {};
        }
        if (d.renderTarget && (d.format == Format::RGBA16F || d.format == Format::RGBA32F) && !m_caps.floatRenderTargets) {
            SetError("CreateTexture: float render targets need OpenGL 3"); return {};
        }
        GLTex t; t.w = d.width; t.h = d.height; t.fmt = d.format; t.rt = d.renderTarget; t.sampled = d.sampled;
        GLint internal; GLenum format, type; FormatOf(d.format, internal, format, type);
        g.GenTextures(1, &t.id);
        if (!t.id) { SetError("CreateTexture: glGenTextures failed"); return {}; }
        g.ActiveTexture(GL_TEXTURE0);
        g.BindTexture(GL_TEXTURE_2D, t.id);
        g.TexImage2D(GL_TEXTURE_2D, 0, internal, d.width, d.height, 0, format, type, d.initialData);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);   // no mip levels: a mipmap filter would make it incomplete
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g.BindTexture(GL_TEXTURE_2D, 0);
        TextureHandle h{NextId()}; m_textures[h.id] = t; return h;
    }
    TextureHandle Wrap(GLuint id, const TextureDesc& d) {
        if (!id || d.width <= 0 || d.height <= 0) { SetError("WrapGLTexture: bad texture"); return {}; }
        GLTex t; t.id = id; t.w = d.width; t.h = d.height; t.fmt = d.format; t.rt = d.renderTarget; t.sampled = d.sampled; t.owned = false;
        TextureHandle h{NextId()}; m_textures[h.id] = t; return h;
    }
    SamplerHandle CreateSampler(const SamplerDesc& d) override { SamplerHandle h{NextId()}; m_samplers[h.id] = d; return h; }
    ShaderHandle CreateShader(const ShaderDesc& d) override;
    PipelineHandle CreatePipeline(const PipelineDesc& d) override {
        if (!m_shaders.count(d.shader.id)) { SetError("CreatePipeline: unknown shader"); return {}; }
        const std::string err = ValidatePipelineDesc(d);
        if (!err.empty()) { SetError("CreatePipeline: " + err); return {}; }
        PipelineHandle h{NextId()}; m_pipes[h.id] = GLPipe{d}; return h;
    }
    SwapChainHandle CreateSwapChain(int width, int height) override {
        if (width <= 0 || height <= 0) { SetError("CreateSwapChain: bad size"); return {}; }
        SwapChainHandle s{NextId()};
        GLTex t; t.id = 0; t.w = width; t.h = height; t.rt = true; t.sampled = false; t.owned = false; t.swapChain = s.id;
        TextureHandle th{NextId()};
        m_textures[th.id] = t;
        m_swap[s.id] = th.id;
        return s;
    }

    bool UpdateBuffer(BufferHandle h, std::size_t offset, const void* data, std::size_t bytes) override {
        auto it = m_buffers.find(h.id);
        if (it == m_buffers.end() || offset + bytes > it->second.size) { SetError("UpdateBuffer: bad buffer or range"); return false; }
        g.BindBuffer(GL_ARRAY_BUFFER, it->second.id);
        if (g.BufferSubData) g.BufferSubData(GL_ARRAY_BUFFER, (GLsizeiptrOK)offset, (GLsizeiptrOK)bytes, data);
        else if (offset == 0 && bytes == it->second.size) g.BufferData(GL_ARRAY_BUFFER, (GLsizeiptrOK)bytes, data, GL_DYNAMIC_DRAW);
        else { g.BindBuffer(GL_ARRAY_BUFFER, 0); SetError("UpdateBuffer: glBufferSubData missing"); return false; }
        g.BindBuffer(GL_ARRAY_BUFFER, 0);
        return true;
    }
    bool UpdateTexture(TextureHandle h, const void* pixels) override {
        auto it = m_textures.find(h.id);
        if (it == m_textures.end() || !it->second.id || !pixels) { SetError("UpdateTexture: bad texture"); return false; }
        const GLTex& t = it->second;
        GLint internal; GLenum format, type; FormatOf(t.fmt, internal, format, type);
        g.ActiveTexture(GL_TEXTURE0);
        g.BindTexture(GL_TEXTURE_2D, t.id);
        g.TexImage2D(GL_TEXTURE_2D, 0, internal, t.w, t.h, 0, format, type, pixels);
        g.BindTexture(GL_TEXTURE_2D, 0);
        return true;
    }
    bool ReadTexture(TextureHandle h, std::vector<std::uint8_t>& out) override {
        auto it = m_textures.find(h.id);
        if (it == m_textures.end() || IsDepth(it->second.fmt)) { SetError("ReadTexture: bad texture (depth cannot be read back on OpenGL)"); return false; }
        const GLTex& t = it->second;
        GLuint fbo = 0;
        if (!t.swapChain && !Framebuffer(h.id, 0, fbo)) return false;
        g.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        const bool fl = t.fmt != Format::RGBA8;
        out.assign((std::size_t)t.w * t.h * (fl ? 16 : 4), 0);
        g.ReadPixels(0, 0, t.w, t.h, GL_RGBA, fl ? GL_FLOAT : GL_UNSIGNED_BYTE, out.data());
        g.BindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }
    bool TextureSize(TextureHandle h, int& w, int& hh) const override {
        auto it = m_textures.find(h.id);
        if (it == m_textures.end()) return false;
        w = it->second.w; hh = it->second.h; return true;
    }
    TextureHandle BackBuffer(SwapChainHandle s) override {
        auto it = m_swap.find(s.id);
        return it == m_swap.end() ? TextureHandle{} : TextureHandle{it->second};
    }
    bool ResizeSwapChain(SwapChainHandle s, int width, int height) override {
        auto it = m_swap.find(s.id);
        if (it == m_swap.end() || width <= 0 || height <= 0) return false;
        GLTex& t = m_textures[it->second]; t.w = width; t.h = height;
        return true;
    }
    bool Present(SwapChainHandle s) override {
        if (!m_swap.count(s.id)) return false;
        if (m_opt.swapBuffers) m_opt.swapBuffers();
        return true;
    }
    std::size_t LiveResources() const override {
        return m_buffers.size() + m_textures.size() + m_samplers.size() + m_shaders.size() + m_pipes.size() + m_swap.size();
    }
    bool Submit(const CommandList& list) override;

protected:
    void Release(Kind k, std::uint32_t id) override {
        switch (k) {
            case Kind::Buffer: { auto it = m_buffers.find(id); if (it != m_buffers.end()) { g.DeleteBuffers(1, &it->second.id); m_buffers.erase(it); } break; }
            case Kind::Texture: {
                auto it = m_textures.find(id);
                if (it == m_textures.end() || it->second.swapChain) break;   // a back buffer goes with its swap chain
                DropFramebuffers(id);
                if (it->second.owned && it->second.id) g.DeleteTextures(1, &it->second.id);
                m_textures.erase(it);
                break;
            }
            case Kind::Sampler: m_samplers.erase(id); break;
            case Kind::Shader: { auto it = m_shaders.find(id); if (it != m_shaders.end()) { g.DeleteProgram(it->second.prog); m_shaders.erase(it); } break; }
            case Kind::Pipeline: m_pipes.erase(id); break;
            case Kind::SwapChain: {
                auto it = m_swap.find(id);
                if (it != m_swap.end()) { m_textures.erase(it->second); m_swap.erase(it); }
                break;
            }
        }
    }

private:
    bool Framebuffer(std::uint32_t color, std::uint32_t depth, GLuint& out);
    void DropFramebuffers(std::uint32_t tex) {
        for (auto it = m_fbos.begin(); it != m_fbos.end();) {
            if (it->first.first == tex || it->first.second == tex) { g.DeleteFramebuffers(1, &it->second); it = m_fbos.erase(it); }
            else ++it;
        }
    }
    void ApplySampler(GLTex& t, const SamplerDesc& s) {
        if (t.appliedValid && std::memcmp(&t.applied, &s, sizeof(SamplerDesc)) == 0) return;
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, s.minFilter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, s.magFilter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, s.wrapU == Wrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, s.wrapV == Wrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        t.applied = s; t.appliedValid = true;
    }

    GLDeviceOptions m_opt;
    Caps m_caps;
    GLuint m_vao = 0;
    bool m_abandoned = false;
    std::unordered_map<std::uint32_t, GLBuf> m_buffers;
    std::unordered_map<std::uint32_t, GLTex> m_textures;
    std::unordered_map<std::uint32_t, SamplerDesc> m_samplers;
    std::unordered_map<std::uint32_t, GLShader> m_shaders;
    std::unordered_map<std::uint32_t, GLPipe> m_pipes;
    std::unordered_map<std::uint32_t, std::uint32_t> m_swap;   // swap chain -> its back buffer texture
    std::map<std::pair<std::uint32_t, std::uint32_t>, GLuint> m_fbos;
};

ShaderHandle GLDevice::CreateShader(const ShaderDesc& d) {
    const std::string err = ValidateShaderDesc(d);
    if (!err.empty()) { SetError("CreateShader: " + err); return {}; }
    if (d.glslVertex.empty() || d.glslFragment.empty()) { SetError(std::string("CreateShader '") + d.debugName + "': no GLSL for the OpenGL backend"); return {}; }
    for (const UniformField& f : d.uniforms)
        if (f.type == UniformType::Vec4 && !g.Uniform4fv) { SetError("CreateShader: vec4 uniforms need glUniform4fv"); return {}; }
    GLuint vs = gl_render::compile(GL_VERTEX_SHADER, d.glslVertex.c_str());
    GLuint fs = gl_render::compile(GL_FRAGMENT_SHADER, d.glslFragment.c_str());
    if (!vs || !fs) {
        if (vs) g.DeleteShader(vs);
        if (fs) g.DeleteShader(fs);
        SetError(std::string("CreateShader '") + d.debugName + "': GLSL did not compile (see the log)");
        return {};
    }
    GLuint prog = g.CreateProgram();
    g.AttachShader(prog, vs); g.AttachShader(prog, fs);
    for (std::size_t i = 0; i < d.attributes.size(); ++i) g.BindAttribLocation(prog, (GLuint)i, d.attributes[i].c_str());
    g.LinkProgram(prog);
    GLint ok = 0; g.GetProgramiv(prog, GL_LINK_STATUS, &ok);
    g.DeleteShader(vs); g.DeleteShader(fs);
    if (!ok) {
        char log[512] = {0}; g.GetProgramInfoLog(prog, 512, nullptr, log);
        Log::Error("[rhi gl] link '", d.debugName, "': ", log);
        g.DeleteProgram(prog);
        SetError(std::string("CreateShader '") + d.debugName + "': link failed");
        return {};
    }
    GLShader s; s.prog = prog; s.fields = d.uniforms;
    for (const UniformField& f : d.uniforms) s.loc.push_back(g.GetUniformLocation(prog, f.name.c_str()));
    g.UseProgram(prog);
    for (std::size_t i = 0; i < d.textures.size(); ++i) {
        const GLint l = g.GetUniformLocation(prog, d.textures[i].c_str());
        if (l >= 0) g.Uniform1i(l, (GLint)i);
    }
    g.UseProgram(0);
    ShaderHandle h{NextId()}; m_shaders[h.id] = std::move(s); return h;
}

bool GLDevice::Framebuffer(std::uint32_t color, std::uint32_t depth, GLuint& out) {
    auto key = std::make_pair(color, depth);
    auto it = m_fbos.find(key);
    if (it != m_fbos.end()) { out = it->second; return true; }
    GLuint fbo = 0;
    g.GenFramebuffers(1, &fbo);
    g.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (color) g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_textures[color].id, 0);
    else if (g.DrawBuffer && g.ReadBuffer) { g.DrawBuffer(GL_NONE); g.ReadBuffer(GL_NONE); }
    if (depth) g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_textures[depth].id, 0);
    const bool complete = g.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    g.BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) { g.DeleteFramebuffers(1, &fbo); SetError("render pass: framebuffer incomplete"); return false; }
    m_fbos[key] = fbo;
    out = fbo;
    return true;
}

bool GLDevice::Submit(const CommandList& list) {
    if (!list.Error().empty()) { SetError("Submit: the list has an error: " + list.Error()); return false; }
    ++m_stats.submits;
    GLint prevVao = 0;
    if (m_vao) { if (g.GetIntegerv) g.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao); g.BindVertexArray(m_vao); }
    const GLShader* shader = nullptr;
    const GLPipe* pipe = nullptr;
    const GLBuf* vb = nullptr; std::uint32_t vbOffset = 0;
    const GLBuf* ib = nullptr; IndexType ibType = IndexType::U16; std::uint32_t ibOffset = 0;
    const std::uint8_t* uniforms = nullptr;
    bool vbDirty = true, uniformsDirty = true;
    unsigned enabledAttribs = 0, usedUnits = 0;
    bool ok = true;
    auto fail = [&](const std::string& e) { SetError("Submit: " + e); ok = false; };

    for (const CommandList::Command& c : list.Commands()) {
        if (!ok) break;
        switch (c.op) {
            case CommandList::Op::Transition: ++m_stats.transitions; break;   // GL tracks hazards itself
            case CommandList::Op::BeginPass: {
                const RenderPassDesc& p = list.Pass(c.a);
                auto ct = p.color ? m_textures.find(p.color.id) : m_textures.end();
                auto dt = p.depth ? m_textures.find(p.depth.id) : m_textures.end();
                if ((p.color && (ct == m_textures.end() || !ct->second.rt || IsDepth(ct->second.fmt))) ||
                    (p.depth && (dt == m_textures.end() || !IsDepth(dt->second.fmt)))) { fail("bad render pass attachment"); break; }
                GLuint fbo = 0;
                const bool backBuffer = p.color && ct->second.swapChain != 0;
                if (backBuffer && p.depth) { fail("a back buffer pass cannot take a depth texture yet"); break; }
                if (!backBuffer && !Framebuffer(p.color.id, p.depth.id, fbo)) { ok = false; break; }
                g.BindFramebuffer(GL_FRAMEBUFFER, fbo);
                const GLTex& any = p.color ? ct->second : dt->second;
                g.Viewport(0, 0, any.w, any.h);
                GLbitfield mask = 0;
                if (p.color && p.colorLoad == LoadOp::Clear) {
                    g.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                    g.ClearColor(p.clearColor[0], p.clearColor[1], p.clearColor[2], p.clearColor[3]);
                    mask |= GL_COLOR_BUFFER_BIT;
                }
                if (p.depth && p.depthLoad == LoadOp::Clear) {
                    g.DepthMask(GL_TRUE);
                    if (g.ClearDepth) g.ClearDepth((GLdouble)p.clearDepth);
                    mask |= GL_DEPTH_BUFFER_BIT;
                }
                if (mask) g.Clear(mask);
                // Everything bound in a pass ends with it (as on every backend): pipeline, buffers, uniforms, textures.
                pipe = nullptr; shader = nullptr; vb = nullptr; ib = nullptr; uniforms = nullptr;
                for (int u = 0; u < kMaxTextureSlots; ++u)
                    if (usedUnits & (1u << u)) { g.ActiveTexture(GL_TEXTURE0 + u); g.BindTexture(GL_TEXTURE_2D, 0); }
                usedUnits = 0;
                ++m_stats.passes;
                break;
            }
            case CommandList::Op::EndPass: break;
            case CommandList::Op::Viewport: g.Viewport((GLint)c.a, (GLint)c.b, (GLsizei)c.c, (GLsizei)c.d); break;
            case CommandList::Op::Pipeline: {
                auto it = m_pipes.find(c.a);
                if (it == m_pipes.end()) { fail("unknown pipeline"); break; }
                auto sh = m_shaders.find(it->second.desc.shader.id);
                if (sh == m_shaders.end()) { fail("the pipeline's shader was destroyed"); break; }
                pipe = &it->second; shader = &sh->second;
                const PipelineDesc& d = pipe->desc;
                g.UseProgram(shader->prog);
                if (d.blend.enable) {
                    g.Enable(GL_BLEND);
                    if (d.blend.srcColor == d.blend.srcAlpha && d.blend.dstColor == d.blend.dstAlpha)
                        g.BlendFunc(BlendGL(d.blend.srcColor), BlendGL(d.blend.dstColor));
                    else if (g.BlendFuncSeparate)
                        g.BlendFuncSeparate(BlendGL(d.blend.srcColor), BlendGL(d.blend.dstColor), BlendGL(d.blend.srcAlpha), BlendGL(d.blend.dstAlpha));
                    else { fail("separate alpha blending needs glBlendFuncSeparate"); break; }
                } else {
                    g.Disable(GL_BLEND);
                }
                const std::uint8_t m = d.blend.writeMask;
                g.ColorMask((m & 1) ? GL_TRUE : GL_FALSE, (m & 2) ? GL_TRUE : GL_FALSE, (m & 4) ? GL_TRUE : GL_FALSE, (m & 8) ? GL_TRUE : GL_FALSE);
                if (d.depth.test) { g.Enable(GL_DEPTH_TEST); g.DepthFunc(CompareGL(d.depth.compare)); }
                else g.Disable(GL_DEPTH_TEST);
                g.DepthMask(d.depth.test && d.depth.write ? GL_TRUE : GL_FALSE);
                if (d.cull == CullMode::None) g.Disable(GL_CULL_FACE);
                else { g.Enable(GL_CULL_FACE); g.CullFace(d.cull == CullMode::Back ? GL_BACK : GL_FRONT); }
                vbDirty = uniformsDirty = true;
                ++m_stats.pipelineBinds;
                break;
            }
            case CommandList::Op::VertexBuffer: {
                auto it = m_buffers.find(c.a);
                if (it == m_buffers.end()) { fail("unknown vertex buffer"); break; }
                vb = &it->second; vbOffset = c.b; vbDirty = true; break;
            }
            case CommandList::Op::IndexBuffer: {
                auto it = m_buffers.find(c.a);
                if (it == m_buffers.end()) { fail("unknown index buffer"); break; }
                ib = &it->second; ibType = (IndexType)c.b; ibOffset = c.c; break;
            }
            case CommandList::Op::Texture: {
                g.ActiveTexture(GL_TEXTURE0 + c.a);
                usedUnits |= 1u << c.a;
                if (c.b == 0) { g.BindTexture(GL_TEXTURE_2D, 0); break; }
                auto it = m_textures.find(c.b);
                auto sm = m_samplers.find(c.c);
                if (it == m_textures.end() || sm == m_samplers.end() || !it->second.id) { fail("unknown texture or sampler"); break; }
                if (!it->second.sampled) { fail("texture was not made to be sampled"); break; }
                g.BindTexture(GL_TEXTURE_2D, it->second.id);
                ApplySampler(it->second, sm->second);
                break;
            }
            case CommandList::Op::Uniforms: uniforms = c.b ? list.UniformData(c.a) : nullptr; uniformsDirty = true; break;
            case CommandList::Op::Draw:
            case CommandList::Op::DrawIndexed: {
                if (!pipe) { fail("draw without a pipeline"); break; }
                const bool indexed = c.op == CommandList::Op::DrawIndexed;
                if (indexed && (!ib || (std::int32_t)c.c != 0)) { fail(ib ? "baseVertex is not supported on OpenGL yet (Caps::drawBaseVertex)" : "DrawIndexed without an index buffer"); break; }
                if (vbDirty) {
                    const VertexLayout& L = pipe->desc.layout;
                    unsigned want = 0;
                    if (!L.attributes.empty()) {
                        if (!vb) { fail("the pipeline has vertex attributes but no vertex buffer is set"); break; }
                        g.BindBuffer(GL_ARRAY_BUFFER, vb->id);
                    }
                    for (const VertexAttribute& a : L.attributes) {
                        GLint n = 4; GLenum type = GL_FLOAT; GLboolean norm = GL_FALSE;
                        switch (a.format) {
                            case VertexFormat::Float1: n = 1; break;
                            case VertexFormat::Float2: n = 2; break;
                            case VertexFormat::Float3: n = 3; break;
                            case VertexFormat::Float4: n = 4; break;
                            case VertexFormat::UNorm8x4: n = 4; type = GL_UNSIGNED_BYTE; norm = GL_TRUE; break;
                        }
                        g.EnableVertexAttribArray((GLuint)a.location);
                        g.VertexAttribPointer((GLuint)a.location, n, type, norm, (GLsizei)L.stride,
                                              (const void*)(std::uintptr_t)(vbOffset + a.offset));
                        want |= 1u << a.location;
                    }
                    for (int l = 0; l < kMaxAttributes; ++l)
                        if ((enabledAttribs & (1u << l)) && !(want & (1u << l))) g.DisableVertexAttribArray((GLuint)l);
                    enabledAttribs = want;
                    vbDirty = false;
                }
                if (uniformsDirty && uniforms) {
                    for (std::size_t i = 0; i < shader->fields.size(); ++i) {
                        const GLint l = shader->loc[i];
                        if (l < 0) continue;
                        const UniformField& f = shader->fields[i];
                        const float* v = reinterpret_cast<const float*>(uniforms + f.offset);
                        switch (f.type) {
                            case UniformType::Float: g.Uniform1f(l, v[0]); break;
                            case UniformType::Vec2: g.Uniform2f(l, v[0], v[1]); break;
                            case UniformType::Vec3: g.Uniform3f(l, v[0], v[1], v[2]); break;
                            case UniformType::Vec4: g.Uniform4fv(l, 1, v); break;
                            case UniformType::Mat4: g.UniformMatrix4fv(l, 1, GL_FALSE, v); break;
                            case UniformType::Int: { std::int32_t iv; std::memcpy(&iv, v, 4); g.Uniform1i(l, iv); break; }
                        }
                    }
                    uniformsDirty = false;
                }
                const std::uint32_t count = c.a - c.a % 3;
                if (indexed) {
                    g.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->id);
                    const std::size_t isz = ibType == IndexType::U16 ? 2 : 4;
                    g.DrawElements(GL_TRIANGLES, (GLsizei)count, ibType == IndexType::U16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                                   (const void*)(std::uintptr_t)(ibOffset + (std::size_t)c.b * isz));
                } else {
                    g.DrawArrays(GL_TRIANGLES, (GLint)c.b, (GLsizei)count);
                }
                ++m_stats.draws;
                m_stats.trianglesIn += count / 3;
                break;
            }
        }
    }
    // Leave GL the way the rest of the engine expects it.
    for (int l = 0; l < kMaxAttributes; ++l) if (enabledAttribs & (1u << l)) g.DisableVertexAttribArray((GLuint)l);
    for (int u = kMaxTextureSlots - 1; u >= 0; --u)
        if (usedUnits & (1u << u)) { g.ActiveTexture(GL_TEXTURE0 + u); g.BindTexture(GL_TEXTURE_2D, 0); }
    g.ActiveTexture(GL_TEXTURE0);
    g.BindBuffer(GL_ARRAY_BUFFER, 0);
    if (!m_vao) g.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    g.UseProgram(0);
    g.BindFramebuffer(GL_FRAMEBUFFER, 0);
    g.Disable(GL_BLEND); g.Disable(GL_DEPTH_TEST); g.Disable(GL_CULL_FACE);
    g.DepthMask(GL_TRUE);
    g.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (m_vao) g.BindVertexArray((GLuint)prevVao);
    return ok;
}

} // namespace

std::unique_ptr<Device> CreateOpenGLDevice(const GLDeviceOptions& opt) {
    if (!g.ok || !g.ColorMask || !g.CullFace || !g.DrawElements) return nullptr;
    return std::unique_ptr<Device>(new GLDevice(opt));
}

TextureHandle WrapGLTexture(Device& dev, unsigned int glTexture, const TextureDesc& d) {
    if (dev.GetBackend() != Backend::OpenGL) return {};
    return static_cast<GLDevice&>(dev).Wrap(glTexture, d);
}

void AbandonGLDevice(Device& dev) {
    if (dev.GetBackend() == Backend::OpenGL) static_cast<GLDevice&>(dev).Abandon();
}

} // namespace rhi
} // namespace okay
