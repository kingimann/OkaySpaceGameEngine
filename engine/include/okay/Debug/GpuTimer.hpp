#pragma once
// GPU timing abstraction (feature map 23.1). A renderer brackets its passes with
// GpuTimer scopes; the timer reports how long each took, possibly a few frames
// later (real GPU timestamp queries resolve asynchronously). This file ships the
// interface, a CPU-fence fallback that works on every renderer, and a null timer.
// No renderer is modified: the hook points are listed below and in
// docs/debug_tools.md for whoever wires them.
//
//     // renderer, once per frame
//     if (GpuTimer* t = ActiveGpuTimer()) t->BeginFrame();
//     { GpuScope s("Shadows"); ...draw shadow casters... }
//     { GpuScope s("Opaque");  ...draw the scene...      }
//     if (GpuTimer* t = ActiveGpuTimer()) t->EndFrame();
//
//     // anywhere, later
//     GpuFrameTiming timing;
//     while (ActiveGpuTimer() && ActiveGpuTimer()->Poll(timing)) Prof().Gpu(timing.totalMs);
//
// Hook points per renderer (a hardware GpuTimer subclass goes here):
//   GLRenderer     RenderToPixels(): BeginFrame before the first GL call of the frame,
//                  scopes around the shadow, scene and post passes (GL_TIMESTAMP or
//                  GL_TIME_ELAPSED queries), EndFrame after the read-back.
//   D3D11Renderer  D3D11_QUERY_TIMESTAMP_DISJOINT around the frame plus a
//                  D3D11_QUERY_TIMESTAMP at each scope edge; resolve a frame or two later.
//   D3D12Renderer  a timestamp query heap, EndQuery at scope edges, ResolveQueryData into a
//                  readback buffer, convert with the queue's timestamp frequency.
//   SoftwareRenderer / ConsoleRenderer
//                  CPU rasterisers: wrap the frame in a scope; the CPU-fence timer is exact.
//
// Not thread safe: use a timer from the render thread only.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace okay {

struct GpuScopeTime {
    std::string name;
    double ms = 0.0;
    int depth = 0;                  // 0 = a top level scope of the frame
};

struct GpuFrameTiming {
    std::uint64_t frame = 0;        // the timer's frame number (counts BeginFrame calls)
    double totalMs = 0.0;           // whole frame, BeginFrame to EndFrame
    std::vector<GpuScopeTime> scopes;   // in the order they began
    bool fromHardware = false;      // true for GPU timestamps, false for a CPU-side measurement
};

class GpuTimer {
public:
    virtual ~GpuTimer() = default;

    virtual const char* Name() const = 0;
    /// True if the numbers are GPU timestamps; false for the CPU-fence fallback.
    virtual bool IsHardware() const = 0;
    /// A disabled timer ignores every call and costs nothing.
    virtual void SetEnabled(bool on) = 0;
    virtual bool Enabled() const = 0;

    virtual void BeginFrame() = 0;
    virtual void BeginScope(const char* name) = 0;
    virtual void EndScope() = 0;
    virtual void EndFrame() = 0;

    /// Take the oldest finished frame. False when none is ready (the GPU may be a
    /// few frames behind, so poll every frame).
    virtual bool Poll(GpuFrameTiming& out) = 0;
};

/// Does nothing and reports nothing. What ActiveGpuTimer() falls back to is a null
/// pointer instead, so callers check it; this is for code that wants an object.
class NullGpuTimer final : public GpuTimer {
public:
    const char* Name() const override { return "none"; }
    bool IsHardware() const override { return false; }
    void SetEnabled(bool) override {}
    bool Enabled() const override { return false; }
    void BeginFrame() override {}
    void BeginScope(const char*) override {}
    void EndScope() override {}
    void EndFrame() override {}
    bool Poll(GpuFrameTiming&) override { return false; }
};

/// Measures with the CPU clock, optionally after waiting for the GPU to drain.
/// `fence` blocks until all submitted GPU work is done (glFinish, a D3D event query
/// wait, a one pixel read-back); each scope edge calls it, so the times include the
/// GPU work of that scope and nothing else. Without a fence the time is only how long
/// the CPU took to submit. Fencing stalls the pipeline, so the timer starts disabled.
/// Results are ready at once: Poll returns frames in order, at most 8 are kept.
class CpuFenceGpuTimer final : public GpuTimer {
public:
    using Fence = std::function<void()>;
    using Clock = std::function<double()>;   // milliseconds, monotonic

    explicit CpuFenceGpuTimer(Fence fence = nullptr, Clock clock = nullptr);

    const char* Name() const override { return m_fence ? "cpu-fence" : "cpu-submit"; }
    bool IsHardware() const override { return false; }
    void SetEnabled(bool on) override { m_enabled = on; if (!on) { m_open.clear(); m_inFrame = false; } }
    bool Enabled() const override { return m_enabled; }
    void BeginFrame() override;
    void BeginScope(const char* name) override;
    void EndScope() override;
    void EndFrame() override;
    bool Poll(GpuFrameTiming& out) override;

    std::uint64_t FrameCount() const { return m_frame; }
    /// Scopes begun but not yet ended (a mismatch shows up here).
    std::size_t OpenScopes() const { return m_open.size(); }

private:
    double Now() const;

    Fence m_fence;
    Clock m_clock;
    bool m_enabled = false;
    bool m_inFrame = false;
    std::uint64_t m_frame = 0;
    double m_frameStart = 0.0;
    GpuFrameTiming m_cur;
    struct Open { std::size_t index; double start; };
    std::vector<Open> m_open;
    std::vector<GpuFrameTiming> m_ready;   // oldest first, small
};

/// The timer renderers report to; null (the default) means no GPU timing.
/// The pointer is not owned. Set it from the thread that renders.
GpuTimer* ActiveGpuTimer();
void SetActiveGpuTimer(GpuTimer* timer);

/// RAII scope on the active timer; free when there is none.
class GpuScope {
public:
    explicit GpuScope(const char* name) : m_timer(ActiveGpuTimer()) { if (m_timer) m_timer->BeginScope(name); }
    ~GpuScope() { if (m_timer) m_timer->EndScope(); }
    GpuScope(const GpuScope&) = delete;
    GpuScope& operator=(const GpuScope&) = delete;
private:
    GpuTimer* m_timer;
};

} // namespace okay
