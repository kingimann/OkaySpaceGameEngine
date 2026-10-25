#pragma once
// ---------------------------------------------------------------------------
// GPU (OpenGL) scene renderer: the GPU's hardware rasterizer
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

class SkinnedMesh;

class GLRenderer {
public:
    using GLGetProc = void* (*)(const char*);

    /// Resolve modern GL entry points via the host loader (idempotent). Returns
    /// false if a required function is missing — then fall back to software.
    /// A current GL context must already exist.
    static bool LoadGL(GLGetProc getProc);
    /// True once LoadGL() has succeeded.
    static bool Available();
    /// True once the program is built with GPU skinning (bone rows in the vertex
    /// shader). The host then sets SkinnedMesh::GpuSkinning() so characters only
    /// build their bone palette on the CPU. GpuSkinJoints: the shader's joint limit.
    bool SupportsGpuSkinning() const;
    int  GpuSkinJoints() const;

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
    bool EnsureDepthProgram();      // depth-only program for the shadow-map pass
    // (Re)create a depth-only FBO + compare-mode depth texture of w x h.
    bool EnsureDepthTarget(unsigned int& fbo, unsigned int& tex, int& curW, int& curH, int w, int h);
    void DrawShadowCasters(const Scene& scene, const Mat4& lightVP, const GameObject* ignore,
                           const struct LightSample* local);
    bool EnsureTargets(int w, int h, int samples);
    void DestroyTargets();   // free ONLY the size-dependent FBO/renderbuffers/resolve
    void DestroyShadow();
    bool EnsurePostProgs();               // fullscreen-quad programs (bloom chain)
    bool EnsureBloomTargets(int w, int h);
    void DestroyBloom();
    bool EnsureSsaoProgs();               // linear-depth + AO + multiply programs
    bool EnsureSsaoTargets(int w, int h);
    void DestroySsao();

    unsigned int m_prog = 0;
    unsigned int m_depthProg = 0;                             // shadow-map depth pass
    unsigned int m_vao = 0, m_vbo = 0, m_ibo = 0;
    unsigned int m_fbo = 0, m_colorRb = 0, m_depthRb = 0;     // MSAA target
    unsigned int m_resolveFbo = 0, m_resolveTex = 0;          // single-sample resolve
    unsigned int m_shadowFbo = 0, m_shadowTex = 0;            // sun cascade atlas (cascades side by side)
    int m_shadowW = 0, m_shadowH = 0;
    unsigned int m_localFbo = 0, m_localTex = 0;              // spot/point light tile atlas (4x4)
    int m_localW = 0, m_localH = 0;
    // Bloom post chain: bright-pass into a half-res ping-pong pair, separable
    // gaussian blur, then additive composite back onto the resolve target.
    unsigned int m_quadVbo = 0;                               // fullscreen quad (2 tris)
    unsigned int m_brightProg = 0, m_blurProg = 0, m_addProg = 0;
    unsigned int m_bloomFboA = 0, m_bloomTexA = 0;
    unsigned int m_bloomFboB = 0, m_bloomTexB = 0;
    int m_bloomW = 0, m_bloomH = 0;
    int m_ubSrc = -1, m_ubThresh = -1;                        // bright-pass uniforms
    int m_ublSrc = -1, m_ublDir = -1;                         // blur uniforms
    int m_uaSrc = -1, m_uaStrength = -1;                      // composite uniforms
    // SSAO: geometry re-rendered at half res as packed linear depth, a spiral
    // depth-difference AO pass, one blur round, multiplicative composite.
    unsigned int m_ssaoDepthProg = 0, m_ssaoProg = 0, m_mulProg = 0;
    unsigned int m_ssaoDepthFbo = 0, m_ssaoDepthTex = 0, m_ssaoDepthRb = 0;
    unsigned int m_ssaoFboA = 0, m_ssaoTexA = 0;
    unsigned int m_ssaoFboB = 0, m_ssaoTexB = 0;
    int m_ssaoW = 0, m_ssaoH = 0;
    int m_usdMVP = -1, m_usdModel = -1, m_usdEye = -1, m_usdFar = -1;   // depth pass
    int m_usDepth = -1, m_usTexel = -1, m_usRadius = -1, m_usFar = -1;  // AO pass
    int m_umSrc = -1, m_umStrength = -1;                                // multiply
    int m_w = 0, m_h = 0, m_samples = 0;
    // Render targets for the OTHER view sizes in use (Scene view, Game view, Camera
    // Preview, Animation preview all differ): parked and swapped back in instead of
    // deleting + re-creating the MSAA / resolve / bloom / SSAO targets every view.
    struct SizeSet {
        int w = 0, h = 0, samples = 0, bloomW = 0, bloomH = 0, ssaoW = 0, ssaoH = 0;
        unsigned int ids[16] = {};
        unsigned lastUse = 0;
    };
    std::vector<SizeSet> m_parkedSets;
    unsigned m_targetUse = 0;
    void SwapSizeSet(SizeSet& s);     // exchange the live targets with `s`
    void DeleteSizeSet(SizeSet& s);
    int m_uMVP = -1, m_uModel = -1, m_uColor = -1, m_uLightDir = -1,
        m_uLightColor = -1, m_uAmbient = -1, m_uEmissive = -1, m_uEye = -1,
        m_uSpecular = -1, m_uShininess = -1, m_uUnlit = -1,
        m_uTex = -1, m_uUseTex = -1, m_uTiling = -1,
        m_uShaderMode = -1, m_uToonBands = -1, m_uRimStr = -1,
        m_uRimColor = -1, m_uGradTop = -1, m_uGradBot = -1, m_uShadowAlpha = -1, m_uAlpha = -1,
        m_uLightCount = -1, m_uLType = -1, m_uLPos = -1, m_uLDir = -1,
        m_uLCol = -1, m_uLRange = -1, m_uLCosOut = -1, m_uLCosIn = -1, m_uLFalloff = -1,
        m_uNormalTex = -1, m_uHasNormal = -1, m_uNormalStrength = -1,
        m_uTexOffset = -1, m_uSpecTex = -1, m_uHasSpecMap = -1,
        m_uAoTex = -1, m_uHasAo = -1, m_uAoStrength = -1,
        m_uMetallic = -1, m_uReflectivity = -1,
        m_uSkyTop = -1, m_uSkyHor = -1, m_uSkyBot = -1, m_uEnvOn = -1,
        m_uFogOn = -1, m_uFogColor = -1, m_uFogStart = -1, m_uFogEnd = -1, m_uTonemap = -1,
        m_uShadowTex = -1, m_uShadowOn = -1, m_uDepthMVP = -1,
        m_uCasVP = -1, m_uCasSphere = -1, m_uCasTexel = -1, m_uCasCount = -1, m_uCasSize = -1,
        m_uShadowSoft = -1, m_uSunIndex = -1, m_uSunStrength = -1, m_uSunBias = -1,
        m_uLocalTex = -1, m_uLocalVP = -1, m_uLShadow = -1, m_uLocalSize = -1, m_uReceive = -1,
        m_uAmbSky = -1, m_uAmbGround = -1,
        m_uSkin = -1, m_uBones = -1, m_udSkin = -1, m_udBones = -1, m_usdSkin = -1, m_usdBones = -1;
    std::vector<float> m_boneRows;          // GPU skinning: 3 rows per joint, scratch
    std::vector<float> m_verts;             // interleaved pos(3)+normal(3)+uv(2) scratch
    std::vector<unsigned int> m_indices;    // scratch index buffer
    std::vector<std::uint32_t> m_pixels;    // read-back result

    // GPU texture cache (texture name/path -> GL texture object), shared shape with
    // the software renderer's cache so the same material textures appear.
    std::unordered_map<std::string, unsigned int> m_texCache;
    unsigned int TextureFor(const std::string& name);   // upload-on-first-use

    // Per-mesh vertex-buffer cache: static geometry (imported OBJ/glTF models,
    // primitives) is expanded + uploaded ONCE and redrawn from GPU memory, instead
    // of being rebuilt on the CPU and re-uploaded every frame — which made large
    // imports crawl at a few FPS. Entries revalidate per frame via an allocation
    // fingerprint (data pointers + sizes) and a sampled content hash, plus an exact
    // full hash about once a second, so in-place edits still show up. Meshes that
    // deform every frame (SkinnedMesh / Character) bypass this cache entirely.
    struct MeshVB {
        unsigned int vbo = 0;
        int vertCount = 0;                                // vertices in the expanded buffer
        const void* vp = nullptr;  std::size_t vn = 0;    // vertices identity
        const void* tp = nullptr;  std::size_t tn = 0;    // triangles identity
        const void* np = nullptr;  std::size_t nn = 0;    // normals identity
        const void* uvp = nullptr; std::size_t uvn = 0;   // uvs identity
        const void* fcp = nullptr; std::size_t fcn = 0;   // face-colors identity
        std::uint64_t sampleHash = 0;                     // cheap, checked every frame
        std::uint64_t fullHash = 0;                       // exact, refreshed periodically
        unsigned lastUse = 0, lastFullHash = 0;           // frame stamps
        std::uint64_t version = 0;                        // SkinnedMesh::version when skinned
        unsigned buildSerial = 0;                         // bumped on every re-expansion
        unsigned int skinVbo = 0;                         // GPU skinning: joints+weights, expanded alike
        unsigned int skinFor = 0; unsigned skinVer = 0;   // the vbo / build it was made for
    };
    std::unordered_map<const void*, MeshVB> m_meshVB;     // key: &MeshRenderer::mesh
    unsigned m_frame = 0;
    // version != 0: a CPU-skinned mesh — rebuilt when the version changes (no hashing).
    MeshVB* EnsureMeshVB(const Mesh& mesh, std::uint64_t version = 0);
    void BindSkin(const SkinnedMesh* skin, const Mesh& mesh, MeshVB* mv, int uSkin, int uBones);
    void BuildExpandedVerts(const Mesh& mesh);            // fill m_verts (14 floats/vertex)
};

} // namespace okay
