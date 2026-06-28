#pragma once
// ---------------------------------------------------------------------------
// GPU scene renderer on Direct3D 12 (Windows only) — the explicit-API sibling of
// D3D11Renderer / GLRenderer. Same contract: RenderToPixels() renders the scene's
// solid meshes on the GPU (depth-tested, per-vertex lit + base-textured) into an
// OFFSCREEN target and reads the result back as a w*h RGBA8 buffer, a DROP-IN for
// RenderMeshesSS / the other GPU renderers.
//
// It owns its OWN isolated D3D12 device, command queue/list and offscreen target
// (no swap chain), so it never touches the host's window device. The whole
// translation unit is guarded by _WIN32, so the Linux build / headless tests never
// reference Direct3D.
//
// Scope (v1): directional + point/spot scene lights, ambient, vertex colours, a
// base colour texture, emissive and per-material alpha (blended). Normal / gloss /
// AO maps, PBR reflections and MSAA are not applied on this path yet — it's an
// OPT-IN alternative to the (default) D3D11 backend.
// ---------------------------------------------------------------------------
#if defined(_WIN32)

#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec3.hpp"

#include <cstdint>

namespace okay {

class D3D12Renderer {
public:
    /// Create the isolated device + pipeline objects. Returns false on failure
    /// (then the host should fall back to D3D11 / OpenGL / software).
    bool Init();
    /// True once Init() succeeded.
    bool Available() const;

    /// Render the scene at w*h into an offscreen depth-tested target and read it
    /// back as a w*h RGBA8 (ABGR8888-compatible) buffer. `samples` is accepted for
    /// signature parity (MSAA not yet applied). `clear*` is the background (alpha 0
    /// = transparent). Returns nullptr on failure; the buffer is owned by this
    /// renderer (valid until the next call).
    const std::uint32_t* RenderToPixels(const Scene& scene, const Mat4& vp, const Vec3& eye,
                                        int w, int h, int samples,
                                        float clearR, float clearG, float clearB, float clearA,
                                        const GameObject* ignore = nullptr);

    /// Release all D3D resources + the device.
    void Destroy();
    ~D3D12Renderer();

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace okay

#endif // _WIN32
