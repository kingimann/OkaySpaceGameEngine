#pragma once
// ---------------------------------------------------------------------------
// GPU (OpenGL) scene renderer — "what Unity uses": the GPU's hardware rasterizer
// with a real depth buffer and multisample (MSAA) anti-aliasing. Crisp,
// perspective-correct, and the GPU does the work instead of the CPU.
//
// This is a DROP-IN alternative to the software rasterizer: RenderToPixels()
// returns a w*h RGBA8 buffer (same shape as RenderMeshesSS), so the host can feed
// it into the exact same texture-upload path it already uses. The renderer lives
// on its own OpenGL context (created by the host on a hidden window), so it never
// interferes with the editor's SDL_Renderer UI.
//
// Loading: gl.h / opengl32 only guarantee GL 1.1, so every modern entry point is
// resolved at runtime through the host's GetProcAddress (e.g. SDL_GL_GetProcAddress).
// Because EVERY GL call goes through a loaded pointer, the engine library has no
// link-time GL dependency — headless unit tests never touch GL.
//
// Stage 1 scope: solid lit meshes (per-pixel directional Lambert + ambient +
// Blinn-Phong specular + emissive). Textures / normal maps / toon / rim / fog come
// in later stages; the shader is structured to grow.
// ---------------------------------------------------------------------------
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec3.hpp"

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>

namespace okay {

class GLRenderer {
public:
    using GLGetProc = void* (*)(const char*);

    /// Resolve modern GL entry points via the host loader (idempotent). Returns
    /// false if a required function is missing — then fall back to software.
    /// A current GL context must already exist.
    static bool LoadGL(GLGetProc getProc);
    /// True once LoadGL() has succeeded.
    static bool Available();

    GLRenderer() = default;
    ~GLRenderer();

    /// Render the scene's active solid meshes on the GPU at w*h with `samples`
    /// (1 = no MSAA) into an offscreen depth-tested framebuffer, resolve MSAA, and
    /// read the result back as a w*h RGBA8 (ABGR8888-compatible) buffer — a drop-in
    /// for RenderMeshesSS. `clear*` is the background (alpha 0 = transparent so a
    /// grid/sky shows through). A GL context must be current. Returns nullptr on
    /// failure. The buffer is owned by this renderer (valid until the next call).
    const std::uint32_t* RenderToPixels(const Scene& scene, const Mat4& vp, const Vec3& eye,
                                        int w, int h, int samples,
                                        float clearR, float clearG, float clearB, float clearA,
                                        const GameObject* ignore = nullptr);

    /// Free all GL objects (call with the context current).
    void Destroy();

private:
    bool EnsureProgram();
    bool EnsureTargets(int w, int h, int samples);
    void DestroyTargets();   // free ONLY the size-dependent FBO/renderbuffers/resolve

    unsigned int m_prog = 0;
    unsigned int m_vao = 0, m_vbo = 0, m_ibo = 0;
    unsigned int m_fbo = 0, m_colorRb = 0, m_depthRb = 0;     // MSAA target
    unsigned int m_resolveFbo = 0, m_resolveTex = 0;          // single-sample resolve
    int m_w = 0, m_h = 0, m_samples = 0;
    int m_uMVP = -1, m_uModel = -1, m_uColor = -1, m_uLightDir = -1,
        m_uLightColor = -1, m_uAmbient = -1, m_uEmissive = -1, m_uEye = -1,
        m_uSpecular = -1, m_uShininess = -1, m_uUnlit = -1,
        m_uTex = -1, m_uUseTex = -1, m_uTiling = -1,
        m_uShaderMode = -1, m_uToonBands = -1, m_uRimStr = -1,
        m_uRimColor = -1, m_uGradTop = -1, m_uGradBot = -1, m_uShadowAlpha = -1,
        m_uLightCount = -1, m_uLType = -1, m_uLPos = -1, m_uLDir = -1,
        m_uLCol = -1, m_uLRange = -1, m_uLCosOut = -1, m_uLCosIn = -1,
        m_uNormalTex = -1, m_uHasNormal = -1, m_uNormalStrength = -1,
        m_uTexOffset = -1, m_uSpecTex = -1, m_uHasSpecMap = -1,
        m_uAoTex = -1, m_uHasAo = -1, m_uAoStrength = -1,
        m_uMetallic = -1, m_uReflectivity = -1,
        m_uSkyTop = -1, m_uSkyHor = -1, m_uSkyBot = -1, m_uEnvOn = -1;
    std::vector<float> m_verts;             // interleaved pos(3)+normal(3)+uv(2) scratch
    std::vector<unsigned int> m_indices;    // scratch index buffer
    std::vector<std::uint32_t> m_pixels;    // read-back result

    // GPU texture cache (texture name/path -> GL texture object), shared shape with
    // the software renderer's cache so the same material textures appear.
    std::unordered_map<std::string, unsigned int> m_texCache;
    unsigned int TextureFor(const std::string& name);   // upload-on-first-use
};

} // namespace okay
