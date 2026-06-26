#pragma once
// ---------------------------------------------------------------------------
// OkayUI Direct3D 11 backend (Windows only).
//
// Renders OkayUI's batched geometry (OkayUI::GetDrawData) through Direct3D 11 — a
// raw DX11 path, separate from the cross-platform SDL_Renderer path. Use it from a
// host that owns a D3D11 device + immediate context and has a render target bound
// (e.g. the standalone okayui_d3d11_demo, or any DX11 app).
//
// This file/translation unit only compiles on Windows; the rest of OkayUI (and the
// engine, and the Linux build) never reference it, so the cross-platform build is
// unaffected.
//
// Usage per frame:
//     OkayUI::BeginFrame(input);
//     ... widgets ...
//     OkayUI::EndFrameData();              // finalize interaction (no SDL draw)
//     backend.Render(ctx, width, height);  // submit the geometry via D3D11
// ---------------------------------------------------------------------------
#if defined(_WIN32)

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace OkayUI {

class D3D11Backend {
public:
    /// Create device-dependent resources (shaders, input layout, buffers, states).
    /// Returns false if anything failed (e.g. shader compile). Safe to call again.
    bool Init(ID3D11Device* device);

    /// Submit the current OkayUI draw data onto the currently-bound render target,
    /// treated as `displayW` x `displayH` logical pixels with a top-left origin.
    void Render(ID3D11DeviceContext* ctx, int displayW, int displayH);

    /// Release all D3D resources.
    void Shutdown();

    ~D3D11Backend() { Shutdown(); }

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace OkayUI

#endif // _WIN32
