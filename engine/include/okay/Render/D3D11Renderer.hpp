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

    /// Release all D3D resources + the device.
    void Destroy();
    ~D3D11Renderer();

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace okay

#endif // _WIN32
