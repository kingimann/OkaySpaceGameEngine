#include "okay/Render/GLRenderer.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Render/Culling.hpp"
#include "okay/Render/MeshHash.hpp"
#include "okay/Render/Visibility.hpp"
#include "okay/Components/LODGroup.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // GetCachedTexture: shares the texture cache
#include "okay/Render/ShadowSetup.hpp"
#include "okay/Render/Atmosphere.hpp"         // sky palette, fog modes, post settings
#include "okay/Render/PostQualityMath.hpp"   // shared bloom soft knee + AO power (UNVERIFIED on a GPU)
#include "okay/Scene/Transform.hpp"
#include "okay/Components/SkinnedMesh.hpp"    // skinned meshes: VB cache keyed on SkinnedMesh::version
#include "okay/Components/Character.hpp"
#include <utility>
#include "okay/Core/Log.hpp"
#include "okay/Debug/MemoryTracker.hpp"   // "Textures (GPU)": bytes of uploaded textures
#include "okay/Core/Time.hpp"
#include "okay/Render/OkayShader.hpp"     // Custom (.okayshader) surfaces

#include "GLRendererInternal.hpp"

namespace okay {
using namespace gl_render;

bool GLRenderer::EnsureProgram() {
    if (m_prog) return true;
    GLuint vs = compile(GL_VERTEX_SHADER, kVert), fs = compile(GL_FRAGMENT_SHADER, kFrag);
    if (!vs || !fs) return false;
    m_prog = g.CreateProgram();
    g.AttachShader(m_prog, vs); g.AttachShader(m_prog, fs);
    g.BindAttribLocation(m_prog, 0, "aPos");
    g.BindAttribLocation(m_prog, 1, "aNormal");
    g.BindAttribLocation(m_prog, 2, "aUV");
    g.BindAttribLocation(m_prog, 3, "aColor");
    g.BindAttribLocation(m_prog, 4, "aTan");
    g.BindAttribLocation(m_prog, 5, "aJoints");
    g.BindAttribLocation(m_prog, 6, "aWeights");
    g.LinkProgram(m_prog);
    GLint ok = 0; g.GetProgramiv(m_prog, GL_LINK_STATUS, &ok);
    g.DeleteShader(vs); g.DeleteShader(fs);
    if (!ok) { char log[512]; g.GetProgramInfoLog(m_prog, 512, nullptr, log); Log::Error("[gl] link: ", log); g.DeleteProgram(m_prog); m_prog = 0; return false; }
    m_uMVP = g.GetUniformLocation(m_prog, "uMVP");
    m_uSkin = g.GetUniformLocation(m_prog, "uSkin");
    m_uBones = g.GetUniformLocation(m_prog, "uBones");
    m_uModel = g.GetUniformLocation(m_prog, "uModel");
    m_uColor = g.GetUniformLocation(m_prog, "uColor");
    m_uLightDir = g.GetUniformLocation(m_prog, "uLightDir");
    m_uLightColor = g.GetUniformLocation(m_prog, "uLightColor");
    m_uAmbient = g.GetUniformLocation(m_prog, "uAmbient");
    m_uEmissive = g.GetUniformLocation(m_prog, "uEmissive");
    m_uEye = g.GetUniformLocation(m_prog, "uEye");
    m_uSpecular = g.GetUniformLocation(m_prog, "uSpecular");
    m_uShininess = g.GetUniformLocation(m_prog, "uShininess");
    m_uUnlit = g.GetUniformLocation(m_prog, "uUnlit");
    m_uTex = g.GetUniformLocation(m_prog, "uTex");
    m_uUseTex = g.GetUniformLocation(m_prog, "uUseTex");
    m_uTiling = g.GetUniformLocation(m_prog, "uTiling");
    m_uShaderMode = g.GetUniformLocation(m_prog, "uShaderMode");
    m_uToonBands = g.GetUniformLocation(m_prog, "uToonBands");
    m_uRimStr = g.GetUniformLocation(m_prog, "uRimStr");
    m_uRimColor = g.GetUniformLocation(m_prog, "uRimColor");
    m_uGradTop = g.GetUniformLocation(m_prog, "uGradTop");
    m_uGradBot = g.GetUniformLocation(m_prog, "uGradBot");
    m_uShadowAlpha = g.GetUniformLocation(m_prog, "uShadowAlpha");
    m_uAlpha = g.GetUniformLocation(m_prog, "uAlpha");
    m_uLightCount = g.GetUniformLocation(m_prog, "uLightCount");
    m_uLType   = g.GetUniformLocation(m_prog, "uLType");
    m_uLPos    = g.GetUniformLocation(m_prog, "uLPos");
    m_uLDir    = g.GetUniformLocation(m_prog, "uLDir");
    m_uLCol    = g.GetUniformLocation(m_prog, "uLCol");
    m_uLRange  = g.GetUniformLocation(m_prog, "uLRange");
    m_uLFalloff = g.GetUniformLocation(m_prog, "uLFalloff");
    m_uLCosOut = g.GetUniformLocation(m_prog, "uLCosOut");
    m_uLCosIn  = g.GetUniformLocation(m_prog, "uLCosIn");
    m_uLRect = g.GetUniformLocation(m_prog, "uLRect"); m_uLRight = g.GetUniformLocation(m_prog, "uLRight");
    m_uLUp = g.GetUniformLocation(m_prog, "uLUp"); m_uCookieTex = g.GetUniformLocation(m_prog, "uCookieTex");
    m_uCookieRows = g.GetUniformLocation(m_prog, "uCookieRows"); m_uSunSoft = g.GetUniformLocation(m_prog, "uSunSoft");
    m_uFadeDist = g.GetUniformLocation(m_prog, "uFadeDist");
    m_uNormalTex = g.GetUniformLocation(m_prog, "uNormalTex");
    m_uHasNormal = g.GetUniformLocation(m_prog, "uHasNormal");
    m_uNormalStrength = g.GetUniformLocation(m_prog, "uNormalStrength");
    m_uTexOffset = g.GetUniformLocation(m_prog, "uTexOffset");
    m_uSpecTex = g.GetUniformLocation(m_prog, "uSpecTex");
    m_uHasSpecMap = g.GetUniformLocation(m_prog, "uHasSpecMap");
    m_uAoTex = g.GetUniformLocation(m_prog, "uAoTex");
    m_uHasAo = g.GetUniformLocation(m_prog, "uHasAo");
    m_uAoStrength = g.GetUniformLocation(m_prog, "uAoStrength");
    m_uMrTex = g.GetUniformLocation(m_prog, "uMrTex");
    m_uHasMR = g.GetUniformLocation(m_prog, "uHasMR");
    m_uEmisTex = g.GetUniformLocation(m_prog, "uEmisTex");
    m_uHasEmis = g.GetUniformLocation(m_prog, "uHasEmis");
    m_uMetallic = g.GetUniformLocation(m_prog, "uMetallic");
    m_uReflectivity = g.GetUniformLocation(m_prog, "uReflectivity");
    m_uSkyTop = g.GetUniformLocation(m_prog, "uSkyTop");
    m_uSkyHor = g.GetUniformLocation(m_prog, "uSkyHor");
    m_uSkyBot = g.GetUniformLocation(m_prog, "uSkyBot");
    m_uEnvOn = g.GetUniformLocation(m_prog, "uEnvOn");
    m_uFogOn = g.GetUniformLocation(m_prog, "uFogOn");
    m_uTonemap = g.GetUniformLocation(m_prog, "uTonemap");
    m_uFogColor = g.GetUniformLocation(m_prog, "uFogColor");
    m_uFogStart = g.GetUniformLocation(m_prog, "uFogStart");
    m_uFogEnd = g.GetUniformLocation(m_prog, "uFogEnd");
    m_uFogMode = g.GetUniformLocation(m_prog, "uFogMode");
    m_uFogDensity = g.GetUniformLocation(m_prog, "uFogDensity");
    m_uFogHeight = g.GetUniformLocation(m_prog, "uFogHeight");
    m_uFogFalloff = g.GetUniformLocation(m_prog, "uFogFalloff");
    m_uHdrOut = g.GetUniformLocation(m_prog, "uHdrOut");
    m_uShadowTex = g.GetUniformLocation(m_prog, "uShadowTex");
    m_uShadowOn = g.GetUniformLocation(m_prog, "uShadowOn");
    m_uCasVP = g.GetUniformLocation(m_prog, "uCasVP");
    m_uCasSphere = g.GetUniformLocation(m_prog, "uCasSphere");
    m_uCasTexel = g.GetUniformLocation(m_prog, "uCasTexel");
    m_uCasCount = g.GetUniformLocation(m_prog, "uCasCount");
    m_uCasSize = g.GetUniformLocation(m_prog, "uCasSize");
    m_uShadowSoft = g.GetUniformLocation(m_prog, "uShadowSoft");
    m_uSunIndex = g.GetUniformLocation(m_prog, "uSunIndex");
    m_uSunStrength = g.GetUniformLocation(m_prog, "uSunStrength");
    m_uSunBias = g.GetUniformLocation(m_prog, "uSunBias");
    m_uLocalTex = g.GetUniformLocation(m_prog, "uLocalTex");
    m_uLocalVP = g.GetUniformLocation(m_prog, "uLocalVP");
    m_uLShadow = g.GetUniformLocation(m_prog, "uLShadow");
    m_uLocalSize = g.GetUniformLocation(m_prog, "uLocalSize");
    m_uReceive = g.GetUniformLocation(m_prog, "uReceive");
    m_uAmbSky = g.GetUniformLocation(m_prog, "uAmbSky");
    m_uAmbGround = g.GetUniformLocation(m_prog, "uAmbGround");
    // The two shadow samplers are sampler2DShadow: give them their own texture
    // units up front so they never alias a sampler2D unit (a draw-time error).
    g.UseProgram(m_prog);
    g.Uniform1i(m_uShadowTex, 4);
    g.Uniform1i(m_uLocalTex, 5);
    g.Uniform1i(m_uMrTex, 6);
    g.Uniform1i(m_uEmisTex, 7);
    if (m_uCookieTex >= 0) g.Uniform1i(m_uCookieTex, 8);   // light masks
    g.UseProgram(0);
    g.GenBuffers(1, &m_vbo);
    // A core-profile context refuses to draw without a bound VAO (and some drivers
    // crash on the attempt). Create one when available; on a compatibility context
    // the default VAO is used instead. This persists across viewport resizes.
    if (g.GenVertexArrays && !m_vao) g.GenVertexArrays(1, &m_vao);
    return true;
}

// ---- Custom (.okayshader) surfaces ------------------------------------------------------------
// The uniforms the per-object draw code sets through m_u* members. While a custom object draws,
// these members hold the custom program's locations (SwapObjectLocations); everything else the
// main program has is copied over once per frame.
namespace {
struct ObjUniform { int GLRenderer::* member; const char* name; };
}
#define OKAY_OBJ_UNIFORMS(X) \
    X(m_uMVP, "uMVP") X(m_uModel, "uModel") X(m_uColor, "uColor") X(m_uEmissive, "uEmissive") \
    X(m_uSpecular, "uSpecular") X(m_uShininess, "uShininess") X(m_uUnlit, "uUnlit") X(m_uReceive, "uReceive") \
    X(m_uShaderMode, "uShaderMode") X(m_uToonBands, "uToonBands") X(m_uRimStr, "uRimStr") X(m_uRimColor, "uRimColor") \
    X(m_uGradTop, "uGradTop") X(m_uGradBot, "uGradBot") X(m_uTex, "uTex") X(m_uUseTex, "uUseTex") X(m_uTiling, "uTiling") \
    X(m_uNormalTex, "uNormalTex") X(m_uHasNormal, "uHasNormal") X(m_uNormalStrength, "uNormalStrength") \
    X(m_uSpecTex, "uSpecTex") X(m_uHasSpecMap, "uHasSpecMap") X(m_uAoTex, "uAoTex") X(m_uHasAo, "uHasAo") \
    X(m_uAoStrength, "uAoStrength") X(m_uHasMR, "uHasMR") X(m_uHasEmis, "uHasEmis") X(m_uMetallic, "uMetallic") \
    X(m_uReflectivity, "uReflectivity") X(m_uTexOffset, "uTexOffset") X(m_uAlpha, "uAlpha") \
    X(m_uShadowAlpha, "uShadowAlpha") X(m_uSkin, "uSkin") X(m_uBones, "uBones")

GLRenderer::CustomGL* GLRenderer::EnsureCustomProgram(const std::string& path) {
    const oshader::ShaderFile sf = oshader::LoadShaderFile(path);
    CustomGL& cp = m_custom[path];
    if (cp.generation == sf.generation && (cp.prog || cp.failed)) return cp.failed ? nullptr : &cp;
    if (cp.prog) { g.DeleteProgram(cp.prog); cp.prog = 0; }
    cp = CustomGL{};
    cp.generation = sf.generation;
    const oshader::Compiled* c = sf.compiled.get();
    if (!c || !c->ok || !m_prog || !g.GetActiveUniform || !g.GetUniformfv || !g.GetUniformiv) {
        cp.failed = true;
        if (c && c->ok) Log::Error("[gl] custom shaders need glGetActiveUniform / glGetUniformfv; ", path, " draws magenta");
        return nullptr;
    }
    // The main fragment shader with the generated surface function spliced in before main().
    std::string frag = kFrag;
    const std::size_t at = frag.find("void main(){");
    if (at == std::string::npos) { cp.failed = true; return nullptr; }
    frag.insert(at, "#define OKAY_SURFACE 1\n" + c->glsl);
    GLuint vs = compile(GL_VERTEX_SHADER, kVert), fs = compile(GL_FRAGMENT_SHADER, frag.c_str());
    if (!vs || !fs) {
        if (vs) g.DeleteShader(vs);
        if (fs) g.DeleteShader(fs);
        Log::Error("[gl] custom shader ", path, " did not compile on this GPU; it draws magenta");
        cp.failed = true;
        return nullptr;
    }
    cp.prog = g.CreateProgram();
    g.AttachShader(cp.prog, vs); g.AttachShader(cp.prog, fs);
    const char* attrs[] = {"aPos", "aNormal", "aUV", "aColor", "aTan", "aJoints", "aWeights"};
    for (int i = 0; i < 7; ++i) g.BindAttribLocation(cp.prog, (GLuint)i, attrs[i]);
    g.LinkProgram(cp.prog);
    GLint ok = 0; g.GetProgramiv(cp.prog, GL_LINK_STATUS, &ok);
    g.DeleteShader(vs); g.DeleteShader(fs);
    if (!ok) {
        char log[512]; g.GetProgramInfoLog(cp.prog, 512, nullptr, log);
        Log::Error("[gl] custom shader ", path, " link: ", log);
        g.DeleteProgram(cp.prog); cp.prog = 0; cp.failed = true;
        return nullptr;
    }
#define OKAY_OBJ_LOC(m, n) cp.locs.push_back(g.GetUniformLocation(cp.prog, n));
    OKAY_OBJ_UNIFORMS(OKAY_OBJ_LOC)
#undef OKAY_OBJ_LOC
    cp.unlit = c->unlit;
    cp.writesAlpha = c->writesAlpha;
    cp.uTime = g.GetUniformLocation(cp.prog, "uOkTime");
    for (const oshader::Property& pr : c->props) cp.uProps.push_back(g.GetUniformLocation(cp.prog, ("okp_" + pr.name).c_str()));
    cp.texCount = (int)c->textures.size();
    g.UseProgram(cp.prog);
    for (int i = 0; i < cp.texCount; ++i) {
        const GLint l = g.GetUniformLocation(cp.prog, ("okt_" + c->textures[(std::size_t)i].name).c_str());
        if (l >= 0) g.Uniform1i(l, oshader::kFirstTextureUnit + i);
    }
    // Everything else the main program has (lights, shadows, fog, sky, samplers): copied each frame.
    GLint count = 0;
    g.GetProgramiv(cp.prog, 0x8B86 /*GL_ACTIVE_UNIFORMS*/, &count);
    for (GLint u = 0; u < count; ++u) {
        char name[128]; GLsizei len = 0; GLint size = 0; GLenum type = 0;
        g.GetActiveUniform(cp.prog, (GLuint)u, sizeof(name), &len, &size, &type, name);
        std::string base(name, (std::size_t)(len > 0 ? len : 0));
        if (base.size() > 3 && base.compare(base.size() - 3, 3, "[0]") == 0) base.resize(base.size() - 3);
        if (base.rfind("okp_", 0) == 0 || base.rfind("okt_", 0) == 0 || base == "uOkTime") continue;
        bool perObject = false;
#define OKAY_OBJ_NAME(m, n) if (base == n) perObject = true;
        OKAY_OBJ_UNIFORMS(OKAY_OBJ_NAME)
#undef OKAY_OBJ_NAME
        if (perObject) continue;
        for (GLint e = 0; e < size; ++e) {
            const std::string nm = size > 1 ? base + "[" + std::to_string(e) + "]" : base;
            CustomGL::Copy cc;
            cc.from = g.GetUniformLocation(m_prog, nm.c_str());
            cc.to = g.GetUniformLocation(cp.prog, nm.c_str());
            cc.type = type;
            if (cc.from >= 0 && cc.to >= 0) cp.frameCopies.push_back(cc);
        }
    }
    g.UseProgram(m_prog);
    return &cp;
}

void GLRenderer::SwapObjectLocations(CustomGL& cp) {
    std::size_t i = 0;
#define OKAY_OBJ_SWAP(m, n) if (i < cp.locs.size()) std::swap(m, cp.locs[i]); ++i;
    OKAY_OBJ_UNIFORMS(OKAY_OBJ_SWAP)
#undef OKAY_OBJ_SWAP
}

void GLRenderer::BeginCustom(CustomGL& cp, const MeshRenderer& mr) {
    g.UseProgram(cp.prog);
    if (cp.syncedFrame != m_frame) {   // the frame's lights, shadows, fog and sky, from the main program
        cp.syncedFrame = m_frame;
        for (const CustomGL::Copy& c : cp.frameCopies) {
            float f[16]; GLint iv[4];
            switch (c.type) {
                case 0x1406: g.GetUniformfv(m_prog, c.from, f); g.Uniform1f(c.to, f[0]); break;                    // float
                case 0x8B50: g.GetUniformfv(m_prog, c.from, f); g.Uniform2f(c.to, f[0], f[1]); break;              // vec2
                case 0x8B51: g.GetUniformfv(m_prog, c.from, f); g.Uniform3f(c.to, f[0], f[1], f[2]); break;        // vec3
                case 0x8B52: if (g.Uniform4fv) { g.GetUniformfv(m_prog, c.from, f); g.Uniform4fv(c.to, 1, f); } break;   // vec4
                case 0x8B5C: g.GetUniformfv(m_prog, c.from, f); g.UniformMatrix4fv(c.to, 1, GL_FALSE, f); break;   // mat4
                case 0x1404: case 0x8B56: case 0x8B5E: case 0x8B62:                                                // int, bool, samplers
                    g.GetUniformiv(m_prog, c.from, iv); g.Uniform1i(c.to, iv[0]); break;
                default: break;
            }
        }
    }
    SwapObjectLocations(cp);
    g.Uniform1f(m_uShadowAlpha, -1.0f);   // the lit draw (the main program has it set once per frame)
    const oshader::ShaderFile sf = oshader::LoadShaderFile(mr.customShader);
    if (cp.uTime >= 0) g.Uniform1f(cp.uTime, Time::ElapsedTime());
    if (sf.compiled && sf.compiled->ok && sf.generation == cp.generation) {
        const std::vector<float> v = oshader::ResolveProperties(*sf.compiled, mr.shaderParams);
        for (std::size_t i = 0; i < cp.uProps.size() && i < sf.compiled->props.size(); ++i) {
            const GLint l = cp.uProps[i];
            if (l < 0) continue;
            const float* p = &v[i * 4];
            switch (oshader::Width(sf.compiled->props[i].type)) {
                case 1: g.Uniform1f(l, p[0]); break;
                case 2: g.Uniform2f(l, p[0], p[1]); break;
                case 3: g.Uniform3f(l, p[0], p[1], p[2]); break;
                default: if (g.Uniform4fv) g.Uniform4fv(l, 1, p); break;
            }
        }
        for (int i = 0; i < cp.texCount && i < oshader::kMaxTextures; ++i) {
            const std::string pic = oshader::ResolveTexture(*sf.compiled, i, mr.shaderParams);
            unsigned int t = pic.empty() ? 0u : TextureFor(pic);
            if (!t) {
                if (!m_whiteTex) {
                    const unsigned char white[4] = {255, 255, 255, 255};
                    g.GenTextures(1, &m_whiteTex);
                    g.BindTexture(GL_TEXTURE_2D, m_whiteTex);
                    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    g.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
                }
                t = m_whiteTex;
            }
            g.ActiveTexture(GL_TEXTURE0 + (GLenum)(oshader::kFirstTextureUnit + i));
            g.BindTexture(GL_TEXTURE_2D, t);
        }
        g.ActiveTexture(GL_TEXTURE0);
    }
}

void GLRenderer::EndCustom(CustomGL& cp) {
    SwapObjectLocations(cp);   // the main program's locations back
    g.UseProgram(m_prog);
}

bool GLRenderer::EnsureDepthProgram() {
    if (m_depthProg) return true;
    GLuint vs = compile(GL_VERTEX_SHADER, kDepthVert), fs = compile(GL_FRAGMENT_SHADER, kDepthFrag);
    if (!vs || !fs) return false;
    m_depthProg = g.CreateProgram();
    g.AttachShader(m_depthProg, vs); g.AttachShader(m_depthProg, fs);
    g.BindAttribLocation(m_depthProg, 0, "aPos");
    g.BindAttribLocation(m_depthProg, 5, "aJoints");
    g.BindAttribLocation(m_depthProg, 6, "aWeights");
    g.LinkProgram(m_depthProg);
    GLint ok = 0; g.GetProgramiv(m_depthProg, GL_LINK_STATUS, &ok);
    g.DeleteShader(vs); g.DeleteShader(fs);
    if (!ok) { g.DeleteProgram(m_depthProg); m_depthProg = 0; return false; }
    m_uDepthMVP = g.GetUniformLocation(m_depthProg, "uDepthMVP");
    m_udSkin = g.GetUniformLocation(m_depthProg, "uSkin");
    m_udBones = g.GetUniformLocation(m_depthProg, "uBones");
    return true;
}

// (Re)create a depth-only framebuffer of w x h: one GL_DEPTH_COMPONENT texture in
// compare mode (sampler2DShadow: each lookup is a filtered depth test).
bool GLRenderer::EnsureDepthTarget(unsigned int& fbo, unsigned int& tex, int& curW, int& curH, int w, int h) {
    if (fbo && curW == w && curH == h) return true;
    if (tex) { g.DeleteTextures(1, &tex); tex = 0; }
    if (fbo) { g.DeleteFramebuffers(1, &fbo); fbo = 0; }
    curW = curH = 0;
    if (!g.DrawBuffer || !g.ReadBuffer) return false;   // can't make a colourless FBO -> no shadows
    g.GenTextures(1, &tex);
    g.BindTexture(GL_TEXTURE_2D, tex);
    g.TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g.TexParameteri(GL_TEXTURE_2D, 0x884C /*GL_TEXTURE_COMPARE_MODE*/, 0x884E /*GL_COMPARE_REF_TO_TEXTURE*/);
    g.TexParameteri(GL_TEXTURE_2D, 0x884D /*GL_TEXTURE_COMPARE_FUNC*/, GL_LEQUAL);
    g.GenFramebuffers(1, &fbo);
    g.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex, 0);
    g.DrawBuffer(0 /*GL_NONE*/);   // depth-only: declare no colour attachments
    g.ReadBuffer(0 /*GL_NONE*/);
    bool okFbo = g.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    g.BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!okFbo) {
        g.DeleteTextures(1, &tex); tex = 0;
        g.DeleteFramebuffers(1, &fbo); fbo = 0;
        return false;
    }
    curW = w; curH = h;
    return true;
}

void GLRenderer::DestroyShadow() {
    if (m_shadowTex) { g.DeleteTextures(1, &m_shadowTex); m_shadowTex = 0; }
    if (m_shadowFbo) { g.DeleteFramebuffers(1, &m_shadowFbo); m_shadowFbo = 0; }
    if (m_localTex) { g.DeleteTextures(1, &m_localTex); m_localTex = 0; }
    if (m_cookieTex) { g.DeleteTextures(1, &m_cookieTex); m_cookieTex = 0; m_cookieCount = 0; }
    if (m_localFbo) { g.DeleteFramebuffers(1, &m_localFbo); m_localFbo = 0; }
    m_shadowW = m_shadowH = m_localW = m_localH = 0;
}
// ---- Full-screen blur (SSAO and the post chain's bloom) ---------------------------
// Bloom WITHOUT the post chain runs on the RHI (rhi::BloomPass, GLRenderer.cpp).
bool GLRenderer::EnsurePostProgs() {
    if (!m_blurProg) {
        m_blurProg = buildQuadProgram(kBlurFrag);
        if (!m_blurProg) return false;
        m_ublSrc = g.GetUniformLocation(m_blurProg, "uSrc");
        m_ublDir = g.GetUniformLocation(m_blurProg, "uDir");
    }
    return EnsureQuad();
}

void GLRenderer::DestroyBloom() {
    if (m_bloomTexA) { g.DeleteTextures(1, &m_bloomTexA); m_bloomTexA = 0; }
    if (m_bloomTexB) { g.DeleteTextures(1, &m_bloomTexB); m_bloomTexB = 0; }
    if (m_bloomFboA) { g.DeleteFramebuffers(1, &m_bloomFboA); m_bloomFboA = 0; }
    if (m_bloomFboB) { g.DeleteFramebuffers(1, &m_bloomFboB); m_bloomFboB = 0; }
    m_bloomW = m_bloomH = 0;
}
// ---- SSAO (screen-space ambient occlusion) ----------------------------------
// Corners, crevices and contact points darken: the scene's opaque geometry is
// re-rendered at half resolution as PACKED LINEAR DEPTH (camera distance / far,
// 16 bits across the R+G channels of an RGBA8 target — GL2-safe, no float
// textures needed), a spiral of depth-difference taps estimates occlusion, one
// gaussian round smooths the noise, and the result multiplies the frame.
// Driven by the same SSAOEnabled()/SSAORadius()/SSAOStrength() globals as the
// software renderer, so the editor's existing AO controls work unchanged.
bool GLRenderer::EnsureSsaoProgs() {
    if (m_ssaoDepthProg && m_ssaoProg && m_mulProg) return true;
    auto build = [&](const char* vsrc, const char* fsrc, unsigned int& prog) -> bool {
        if (prog) return true;
        GLuint vs = compile(GL_VERTEX_SHADER, vsrc), fs = compile(GL_FRAGMENT_SHADER, fsrc);
        if (!vs || !fs) return false;
        prog = g.CreateProgram();
        g.AttachShader(prog, vs); g.AttachShader(prog, fs);
        g.BindAttribLocation(prog, 0, "aPos");
        g.BindAttribLocation(prog, 5, "aJoints");
        g.BindAttribLocation(prog, 6, "aWeights");
        g.LinkProgram(prog);
        GLint ok = 0; g.GetProgramiv(prog, GL_LINK_STATUS, &ok);
        g.DeleteShader(vs); g.DeleteShader(fs);
        if (!ok) { g.DeleteProgram(prog); prog = 0; return false; }
        return true;
    };
    if (!build(kSsaoDepthVert, kSsaoDepthFrag, m_ssaoDepthProg)) return false;
    if (!build(kQuadVert, kSsaoFrag, m_ssaoProg)) return false;
    if (!build(kQuadVert, kMulFrag, m_mulProg)) return false;
    m_usdMVP = g.GetUniformLocation(m_ssaoDepthProg, "uMVP");
    m_usdSkin = g.GetUniformLocation(m_ssaoDepthProg, "uSkin");
    m_usdBones = g.GetUniformLocation(m_ssaoDepthProg, "uBones");
    m_usdModel = g.GetUniformLocation(m_ssaoDepthProg, "uModel");
    m_usdEye = g.GetUniformLocation(m_ssaoDepthProg, "uEye");
    m_usdFar = g.GetUniformLocation(m_ssaoDepthProg, "uFar");
    m_usDepth = g.GetUniformLocation(m_ssaoProg, "uDepth");
    m_usTexel = g.GetUniformLocation(m_ssaoProg, "uTexel");
    m_usRadius = g.GetUniformLocation(m_ssaoProg, "uRadius");
    m_usFar = g.GetUniformLocation(m_ssaoProg, "uFar");
    m_umSrc = g.GetUniformLocation(m_mulProg, "uSrc");
    m_umStrength = g.GetUniformLocation(m_mulProg, "uStrength"); m_umPower = g.GetUniformLocation(m_mulProg, "uPower");
    return true;
}

bool GLRenderer::EnsureSsaoTargets(int w, int h) {
    if (m_ssaoDepthFbo && m_ssaoW == w && m_ssaoH == h) return true;
    DestroySsao();
    m_ssaoW = w; m_ssaoH = h;
    auto makeColorTarget = [&](unsigned int& fbo, unsigned int& tex, bool withDepth) -> bool {
        g.GenTextures(1, &tex);
        g.BindTexture(GL_TEXTURE_2D, tex);
        g.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g.GenFramebuffers(1, &fbo);
        g.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        if (withDepth) {                                  // z-test the geometry pass
            g.GenRenderbuffers(1, &m_ssaoDepthRb);
            g.BindRenderbuffer(GL_RENDERBUFFER, m_ssaoDepthRb);
            g.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
            g.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_ssaoDepthRb);
        }
        bool ok = g.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        g.BindFramebuffer(GL_FRAMEBUFFER, 0);
        return ok;
    };
    if (!makeColorTarget(m_ssaoDepthFbo, m_ssaoDepthTex, true) ||
        !makeColorTarget(m_ssaoFboA, m_ssaoTexA, false) ||
        !makeColorTarget(m_ssaoFboB, m_ssaoTexB, false)) {
        DestroySsao();
        return false;
    }
    return true;
}

void GLRenderer::DestroySsao() {
    if (m_ssaoDepthTex) { g.DeleteTextures(1, &m_ssaoDepthTex); m_ssaoDepthTex = 0; }
    if (m_ssaoTexA) { g.DeleteTextures(1, &m_ssaoTexA); m_ssaoTexA = 0; }
    if (m_ssaoTexB) { g.DeleteTextures(1, &m_ssaoTexB); m_ssaoTexB = 0; }
    if (m_ssaoDepthRb) { g.DeleteRenderbuffers(1, &m_ssaoDepthRb); m_ssaoDepthRb = 0; }
    if (m_ssaoDepthFbo) { g.DeleteFramebuffers(1, &m_ssaoDepthFbo); m_ssaoDepthFbo = 0; }
    if (m_ssaoFboA) { g.DeleteFramebuffers(1, &m_ssaoFboA); m_ssaoFboA = 0; }
    if (m_ssaoFboB) { g.DeleteFramebuffers(1, &m_ssaoFboB); m_ssaoFboB = 0; }
    m_ssaoW = m_ssaoH = 0;
}
// ---- Procedural sky + the scene's post chain ------------------------------------
// The sky is a fullscreen pass drawn before the meshes (depth off): every pixel
// unprojects to a world direction, so it follows the camera's rotation and field
// of view, and its colours come from the sun's height (SkyPaletteFor). The post
// chain runs when PostActive(): the scene resolves from a float target, then
// SSAO -> bloom (half + quarter res) -> exposure / tonemap / grading / vignette
// -> depth of field -> edge smoothing, ending in an RGBA8 texture the host reads back.
bool GLRenderer::EnsureQuad() {
    if (m_quadVbo) return true;
    static const float quad[12] = {-1,-1,  1,-1,  1,1,  -1,-1,  1,1,  -1,1};
    g.GenBuffers(1, &m_quadVbo);
    g.BindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    g.BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    return m_quadVbo != 0;
}

void GLRenderer::DrawQuad() {
    g.BindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    g.EnableVertexAttribArray(0);
    g.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void*)0);
    g.DisableVertexAttribArray(1); g.DisableVertexAttribArray(2);
    g.DisableVertexAttribArray(3); g.DisableVertexAttribArray(4);
    g.DisableVertexAttribArray(5); g.DisableVertexAttribArray(6);
    g.DrawArrays(GL_TRIANGLES, 0, 6);
}

bool GLRenderer::EnsureSkyProgram() {
    if (m_skyProg) return true;
    m_skyProg = buildQuadProgram(kSkyFrag);
    if (!m_skyProg) return false;
    auto u = [&](const char* n) { return g.GetUniformLocation(m_skyProg, n); };
    m_uskInvVP = u("uInvVP"); m_uskEye = u("uEye"); m_uskSun = u("uSunDir");
    m_uskZenith = u("uZenith"); m_uskHorizon = u("uHorizon"); m_uskGround = u("uGround"); m_uskSunColor = u("uSunColor");
    m_uskSunSize = u("uSunSize"); m_uskSunHalo = u("uSunHalo"); m_uskSunDisc = u("uSunDisc"); m_uskHaze = u("uHaze");
    m_uskNight = u("uNight"); m_uskStarDensity = u("uStarDensity"); m_uskStarBright = u("uStarBright");
    m_uskCloudCover = u("uCloudCover"); m_uskCloudDensity = u("uCloudDensity"); m_uskCloudScale = u("uCloudScale");
    m_uskCloudOffset = u("uCloudOffset"); m_uskTime = u("uTime"); m_uskLin = u("uLin");
    m_uskMode = u("uMode"); m_uskTop = u("uTop"); m_uskHor = u("uHor"); m_uskBot = u("uBot"); m_uskHorizonPos = u("uHorizonPos");
    m_uskSunOn = u("uSunOn"); m_uskSunPos = u("uSunPos"); m_uskSunRad = u("uSunRad"); m_uskStarsOn = u("uStarsOn"); m_uskViewSize = u("uViewSize");
    return true;
}

bool GLRenderer::EnsurePostChain() {
    if (m_uberProg && m_edgeSmoothProg && m_dofProg && m_dofCopyProg && m_dofBlurProg && m_downProg) return true;
    if (!EnsurePostProgs()) return false;   // the shared blur program + the quad
    if (!m_uberProg) {
        m_uberProg = buildQuadProgram(kUberFrag);
        if (!m_uberProg) return false;
        auto u = [&](const char* n) { return g.GetUniformLocation(m_uberProg, n); };
        m_uuSrc = u("uSrc"); m_uuBloomA = u("uBloomA"); m_uuBloomC = u("uBloomC");
        m_uuExposure = u("uExposure"); m_uuLinearIn = u("uLinearIn"); m_uuTonemap = u("uTonemap");
        m_uuBloomOn = u("uBloomOn"); m_uuBloomIntensity = u("uBloomIntensity");
        m_uuGradeOn = u("uGradeOn"); m_uuTemperature = u("uTemperature"); m_uuTint = u("uTint");
        m_uuSaturation = u("uSaturation"); m_uuContrast = u("uContrast");
        m_uuLift = u("uLift"); m_uuGamma = u("uGamma"); m_uuGain = u("uGain");
        m_uuVignette = u("uVignette"); m_uuVignetteSmooth = u("uVignetteSmooth"); m_uuAspect = u("uAspect");
    }
    if (!m_edgeSmoothProg) {
        m_edgeSmoothProg = buildQuadProgram(kEdgeSmoothFrag);
        if (!m_edgeSmoothProg) return false;
        m_ufSrc = g.GetUniformLocation(m_edgeSmoothProg, "uSrc"); m_ufTexel = g.GetUniformLocation(m_edgeSmoothProg, "uTexel");
    }
    if (!m_dofProg) {
        m_dofProg = buildQuadProgram(kDofFrag);
        if (!m_dofProg) return false;
        auto u = [&](const char* n) { return g.GetUniformLocation(m_dofProg, n); };
        m_udfSrc = u("uSrc"); m_udfBlur = u("uBlur"); m_udfDepth = u("uDepth");
        m_udfFocus = u("uFocus"); m_udfRange = u("uRange"); m_udfFar = u("uFar");
    }
    if (!m_dofCopyProg) {
        m_dofCopyProg = buildQuadProgram(kDofCopyFrag);
        if (!m_dofCopyProg) return false;
        m_udcSrc = g.GetUniformLocation(m_dofCopyProg, "uSrc");
    }
    if (!m_dofBlurProg) {
        m_dofBlurProg = buildQuadProgram(kDofBlurFrag);
        if (!m_dofBlurProg) return false;
        m_udbSrc = g.GetUniformLocation(m_dofBlurProg, "uSrc"); m_udbDir = g.GetUniformLocation(m_dofBlurProg, "uDir");
    }
    if (!m_downProg) {
        m_downProg = buildQuadProgram(kDownFrag);
        if (!m_downProg) return false;
        auto u = [&](const char* n) { return g.GetUniformLocation(m_downProg, n); };
        m_udnSrc = u("uSrc"); m_udnTexel = u("uTexel"); m_udnThresh = u("uThresh"); m_udnRadius = u("uRadius"); m_udnSoft = u("uSoft");
    }
    return true;
}

// The chain's own targets: a full-res RGBA8 output + ping-pong pair, and the
// bloom pairs (half res, reusing the bloom A/B targets, plus a quarter-res C/D)
// in the frame's format.
bool GLRenderer::EnsurePostTargets(int w, int h, bool hdr) {
    if (m_outFbo && m_postW == w && m_postH == h && m_postHdr == hdr) return true;
    DestroyPostTargets();
    DestroyBloom();
    m_postW = w; m_postH = h; m_postHdr = hdr;
    const int hw = w / 2 > 0 ? w / 2 : 1, hh = h / 2 > 0 ? h / 2 : 1;
    const int qw = w / 4 > 0 ? w / 4 : 1, qh = h / 4 > 0 ? h / 4 : 1;
    bool ok = makeColorTarget(m_outFbo, m_outTex, w, h, false) &&
              makeColorTarget(m_pingFbo[0], m_pingTex[0], w, h, false) &&
              makeColorTarget(m_pingFbo[1], m_pingTex[1], w, h, false) &&
              makeColorTarget(m_bloomFboA, m_bloomTexA, hw, hh, hdr) &&
              makeColorTarget(m_bloomFboB, m_bloomTexB, hw, hh, hdr) &&
              makeColorTarget(m_bloomFboC, m_bloomTexC, qw, qh, hdr) &&
              makeColorTarget(m_bloomFboD, m_bloomTexD, qw, qh, hdr);
    if (!ok) { DestroyPostTargets(); DestroyBloom(); return false; }
    m_bloomW = hw; m_bloomH = hh;
    return true;
}

void GLRenderer::DestroyPostTargets() {
    unsigned int* tex[] = {&m_outTex, &m_pingTex[0], &m_pingTex[1], &m_bloomTexC, &m_bloomTexD};
    unsigned int* fbo[] = {&m_outFbo, &m_pingFbo[0], &m_pingFbo[1], &m_bloomFboC, &m_bloomFboD};
    for (unsigned int* t : tex) if (*t) { g.DeleteTextures(1, t); *t = 0; }
    for (unsigned int* f : fbo) if (*f) { g.DeleteFramebuffers(1, f); *f = 0; }
    m_postW = m_postH = 0;
}

} // namespace okay
