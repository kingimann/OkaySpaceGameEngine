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

    unsigned int m_prog = 0;
    unsigned int m_vao = 0, m_vbo = 0, m_ibo = 0;
    unsigned int m_fbo = 0, m_colorRb = 0, m_depthRb = 0;     // MSAA target
    unsigned int m_resolveFbo = 0, m_resolveTex = 0;          // single-sample resolve
    int m_w = 0, m_h = 0, m_samples = 0;
    int m_uMVP = -1, m_uModel = -1, m_uColor = -1, m_uLightDir = -1,
        m_uLightColor = -1, m_uAmbient = -1, m_uEmissive = -1, m_uEye = -1,
        m_uSpecular = -1, m_uShininess = -1, m_uUnlit = -1;
    std::vector<float> m_verts;             // interleaved pos(3)+normal(3) scratch
    std::vector<unsigned int> m_indices;    // scratch index buffer
    std::vector<std::uint32_t> m_pixels;    // read-back result
};

} // namespace okay
