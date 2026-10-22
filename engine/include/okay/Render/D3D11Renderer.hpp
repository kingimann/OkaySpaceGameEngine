#pragma once
// ---------------------------------------------------------------------------
// GPU scene renderer on Direct3D 11 (Windows only) — the DirectX sibling of
// GLRenderer. Same contract: RenderToPixels() renders the scene's solid meshes on
// the GPU (depth-tested, MSAA, per-pixel lit + textured) and reads the result back
// as a w*h RGBA8 buffer, a DROP-IN for RenderMeshesSS / GLRenderer so the editor
// feeds it through the exact same texture-upload path.
//
// It owns its OWN isolated D3D11 device and renders to an OFFSCREEN target (no
// swapchain), so it never touches the editor's SDL_Renderer device — mirroring how
// GLRenderer uses an isolated GL context. The whole translation unit is guarded by
// _WIN32, so the Linux build / headless tests never reference Direct3D.
// ---------------------------------------------------------------------------
#if defined(_WIN32)

#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec3.hpp"

#include <cstdint>

namespace okay {

class D3D11Renderer {
public:
    /// Create the isolated device + pipeline objects. Returns false on failure
    /// (then the host should fall back to software). Safe to call once.
    bool Init();
    /// True once Init() succeeded.
    bool Available() const;

    /// Render the scene at w*h with `samples` MSAA into an offscreen depth-tested
    /// target, resolve, and read back as a w*h RGBA8 (ABGR8888-compatible) buffer.
    /// `clear*` is the background (alpha 0 = transparent). Returns nullptr on failure.
    /// The buffer is owned by this renderer (valid until the next call).
    const std::uint32_t* RenderToPixels(const Scene& scene, const Mat4& vp, const Vec3& eye,
                                        int w, int h, int samples,
                                        float clearR, float clearG, float clearB, float clearA,
                                        const GameObject* ignore = nullptr);

    /// Pipelined read-back for a live view redrawn every frame (Scene / Game view):
    /// give the view a stable id (>= 0) before RenderToPixels and the call returns
    /// that view's PREVIOUS frame instead of stalling the CPU until the GPU has
    /// finished this one (CPU and GPU then overlap; one frame of latency). The
    /// first frame, a resize, or a view idle for a moment reads synchronously.
    /// -1 (the default) = synchronous, for one-off renders.
    void SetReadbackStream(int id);

    /// Build on an EXISTING device + immediate context (e.g. SDL's Direct3D 11
    /// renderer: SDL_RenderGetD3D11Device). Then RenderToTexture can draw straight
    /// into textures that device presents, with no CPU read-back. Takes its own
    /// references; `device` is an ID3D11Device*.
    bool InitShared(void* device);
    /// True when built on a shared device (InitShared).
    bool Shared() const;
    /// The GPU skinning shaders compiled and are ready (see SkinnedMesh::GpuSkinning).
    bool SupportsGpuSkinning() const;
    /// Render like RenderToPixels, but resolve into `dstTexture` (an
    /// ID3D11Texture2D* of the shared device, w*h, R8G8B8A8_UNORM, not
    /// multisampled) instead of reading back. Every piece of pipeline state the
    /// host renderer may have cached is saved first and restored after.
    bool RenderToTexture(const Scene& scene, const Mat4& vp, const Vec3& eye,
                         int w, int h, int samples,
                         float clearR, float clearG, float clearB, float clearA,
                         void* dstTexture, const GameObject* ignore = nullptr);

    /// The texture behind the render target currently bound on the shared
    /// context (an ID3D11Texture2D*, with a reference you release with
    /// ReleaseCom), or nullptr. Used to find the D3D texture of an SDL render
    /// target right after SDL has drawn into it.
    void* BoundRenderTargetTexture();
    /// Release a COM object handed out above (or by SDL).
    static void ReleaseCom(void* unknown);

    /// Release all D3D resources + the device.
    void Destroy();
    ~D3D11Renderer();

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace okay

#endif // _WIN32
