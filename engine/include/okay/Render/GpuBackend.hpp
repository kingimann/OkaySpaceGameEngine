#pragma once
// ---------------------------------------------------------------------------
// One contract for the GPU scene renderers. GLRenderer, D3D11Renderer and
// D3D12Renderer each expose RenderToPixels() with no common base; this header
// adds the common base (IGpuBackend) and a thin adapter that wraps any of them
// WITHOUT changing what they draw. The chooser (which backend, in which order)
// lives in GpuBackendChooser.hpp and has no Scene dependency.
//
// Written from the three existing renderer headers (their RenderToPixels
// signature and Init / Destroy calls). Nothing is copied from another project.
// A future Vulkan renderer would implement IGpuBackend directly.
//
// The adapter is a template over the renderer type so the contract is unit tested
// headless with a fake renderer (tests/test_gpu_backend.cpp); the real renderers
// are only instantiated by the hosts. No <mutex>, <thread> or condition_variable.
// ---------------------------------------------------------------------------
#include "okay/Render/GpuBackendChooser.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <functional>
#include <utility>

namespace okay {

class Scene;
class GameObject;

/// Everything one RenderToPixels call takes, in one place.
struct GpuFrameDesc {
    const Scene* scene = nullptr;
    Mat4 viewProj;
    Vec3 eye;
    int width = 0, height = 0;
    int samples = 4;                       // MSAA request (1 = none)
    float clearR = 0, clearG = 0, clearB = 0, clearA = 0;   // alpha 0 = transparent
    const GameObject* ignore = nullptr;
};

class IGpuBackend {
public:
    virtual ~IGpuBackend() = default;
    virtual RenderBackend Id() const = 0;
    virtual const char* Name() const = 0;
    virtual const GpuCapabilities& Capabilities() const = 0;
    /// Create the device / resources. false = this backend cannot run here.
    virtual bool Init() = 0;
    virtual bool Ready() const = 0;
    virtual void Shutdown() = 0;
    /// Render one frame and read it back as w*h RGBA8 (ABGR8888-compatible).
    /// The buffer is owned by the backend and valid until the next call.
    /// nullptr = the frame failed (the host counts it and falls through).
    virtual const std::uint32_t* RenderToPixels(const GpuFrameDesc& f) = 0;
};

/// Wraps an existing renderer object (not owned) so it satisfies IGpuBackend.
/// Hooks let a host keep its own context handling: OpenGL needs its hidden
/// context made current around each frame (`acquire` / `release`) and is created
/// by the host (`init` runs the host's own setup instead of R::Init).
template <class R>
class GpuRendererAdapter : public IGpuBackend {
public:
    struct Hooks {
        std::function<bool()> init;       // empty = nothing to do (the host already built the renderer)
        std::function<void()> shutdown;   // empty: call renderer->Destroy()
        std::function<bool()> acquire;    // before a frame; false = frame fails (e.g. MakeCurrent failed)
        std::function<void()> release;    // after a frame
    };
    GpuRendererAdapter(RenderBackend id, R* renderer, Hooks hooks = {})
        : id_(id), r_(renderer), hooks_(std::move(hooks)) {}

    RenderBackend Id() const override { return id_; }
    const char* Name() const override { return GpuBackendLabel(id_); }
    const GpuCapabilities& Capabilities() const override { return GpuBackendCapabilities(id_); }
    bool Init() override {
        ready_ = r_ != nullptr && (hooks_.init ? hooks_.init() : true);
        return ready_;
    }
    bool Ready() const override { return ready_ && r_ != nullptr; }
    void Shutdown() override {
        if (!r_) return;
        if (hooks_.shutdown) hooks_.shutdown(); else r_->Destroy();
        ready_ = false;
    }
    const std::uint32_t* RenderToPixels(const GpuFrameDesc& f) override {
        if (!Ready() || !f.scene) return nullptr;
        if (hooks_.acquire && !hooks_.acquire()) return nullptr;
        const std::uint32_t* px = r_->RenderToPixels(*f.scene, f.viewProj, f.eye, f.width, f.height, f.samples,
                                                     f.clearR, f.clearG, f.clearB, f.clearA, f.ignore);
        if (hooks_.release) hooks_.release();
        return px;
    }
private:
    RenderBackend id_;
    R* r_;
    Hooks hooks_;
    bool ready_ = false;
};

}  // namespace okay
