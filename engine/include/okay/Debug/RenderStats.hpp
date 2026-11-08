#pragma once
// Per-frame render statistics (feature map 23.1): draw calls, triangles, state
// switches and a texture memory estimate. Renderers feed a RenderStatsCollector
// through a handful of one-line calls; the debug overlay and the editor read the
// result. No renderer is modified by this file: add the calls where noted in
// docs/debug_tools.md.
//
//     RenderStatsCollector& rs = RenderStatsCollector::Global();
//     rs.BeginFrame();
//     rs.BindMaterial(RenderStatsCollector::IdOf(&material));   // counts only when it changes
//     rs.DrawCall(mesh.TriangleCount(), mesh.VertexCount());
//     rs.EndFrame();
//     GpuFrameStats s = rs.Last();                              // draw calls, triangles, ...
//
// Every call is lock free (relaxed atomics), so a renderer may report from a
// worker thread. Reading Last()/History() takes a short spin lock.
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "okay/Debug/DebugUtil.hpp"

namespace okay {

class Profiler;

/// What one frame cost on the render side. Counters are per frame; the two byte
/// figures are the live total at the end of the frame.
struct GpuFrameStats {
    std::uint64_t frame = 0;
    std::uint32_t drawCalls = 0;
    std::uint32_t dispatches = 0;           // compute dispatches
    std::uint64_t triangles = 0;
    std::uint64_t vertices = 0;
    std::uint64_t instances = 0;            // objects drawn (an instanced call counts every instance)
    std::uint32_t materialSwitches = 0;     // binds that changed the bound material
    std::uint32_t shaderSwitches = 0;
    std::uint32_t textureSwitches = 0;
    std::uint32_t renderTargetSwitches = 0;
    std::uint64_t textureBytes = 0;         // estimated GPU texture memory in use
    std::uint64_t meshBytes = 0;            // estimated vertex/index buffer memory in use
    double gpuMs = 0.0;                     // from a GpuTimer, 0 if none reported

    /// Rough size of a texture: w*h*bytesPerPixel*layers, +1/3 for a mip chain.
    static std::uint64_t EstimateTextureBytes(int width, int height, int bytesPerPixel = 4,
                                              bool mipmaps = true, int layers = 1);
    std::string ToText() const;
    std::string ToJson() const;
};

class RenderStatsCollector {
public:
    explicit RenderStatsCollector(std::size_t historyFrames = 240);
    RenderStatsCollector(const RenderStatsCollector&) = delete;
    RenderStatsCollector& operator=(const RenderStatsCollector&) = delete;

    /// A process-wide collector for renderers that have no instance to hand.
    static RenderStatsCollector& Global();
    /// A stable id for a bound object (a material or texture pointer).
    static std::uint64_t IdOf(const void* p) { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p)); }

    // ---- renderer side ---------------------------------------------------
    /// Start a frame: per-frame counters go to zero and nothing is bound.
    void BeginFrame();
    /// Finish it: the counters become Last() and join History().
    void EndFrame();
    /// One draw call. `triangles` and `vertices` are for a single instance; the frame
    /// totals multiply them by `instances`.
    void DrawCall(std::uint64_t triangles, std::uint64_t vertices = 0, std::uint32_t instances = 1);
    void Dispatch();
    /// Report what is bound; a switch is counted when the id differs from the one
    /// bound before it in this frame (the first bind of a frame counts as one).
    void BindMaterial(std::uint64_t id);
    void BindShader(std::uint64_t id);
    void BindTexture(std::uint64_t id);
    void BindRenderTarget(std::uint64_t id);
    /// Persistent GPU memory estimates (not reset each frame). Destroyed clamps at 0.
    void TextureCreated(std::uint64_t bytes);
    void TextureDestroyed(std::uint64_t bytes);
    void MeshCreated(std::uint64_t bytes);
    void MeshDestroyed(std::uint64_t bytes);
    /// GPU time of the frame in progress (from a GpuTimer).
    void SetGpuMs(double ms) { m_gpuMs.store(ms, std::memory_order_relaxed); }

    // ---- reading ---------------------------------------------------------
    /// The frame in progress (counters so far).
    GpuFrameStats Current() const;
    /// The last finished frame (all zero before the first EndFrame).
    GpuFrameStats Last() const;
    /// Finished frames, oldest first; at most maxFrames (0 = all kept).
    std::vector<GpuFrameStats> History(std::size_t maxFrames = 0) const;
    std::uint64_t Frame() const { return m_frame.load(std::memory_order_relaxed); }
    void Reset();
    /// Copy the last frame's object/triangle counts into the Profiler's live
    /// stats (Profiler::Stat3) so profiler frames carry them. `particles` is passed through.
    void ApplyToProfiler(Profiler& p, int particles = 0) const;

private:
    static constexpr std::uint64_t kNone = ~static_cast<std::uint64_t>(0);
    static bool Switch(std::atomic<std::uint64_t>& bound, std::uint64_t id) {
        return bound.exchange(id, std::memory_order_relaxed) != id;
    }

    std::atomic<std::uint32_t> m_draws{0}, m_dispatches{0}, m_matSw{0}, m_shaderSw{0}, m_texSw{0}, m_rtSw{0};
    std::atomic<std::uint64_t> m_tris{0}, m_verts{0}, m_inst{0};
    std::atomic<std::uint64_t> m_boundMat{kNone}, m_boundShader{kNone}, m_boundTex{kNone}, m_boundRt{kNone};
    std::atomic<std::uint64_t> m_texBytes{0}, m_meshBytes{0};
    std::atomic<double> m_gpuMs{0.0};
    std::atomic<std::uint64_t> m_frame{0};

    mutable debugutil::SpinLock m_lock;
    GpuFrameStats m_last;
    std::vector<GpuFrameStats> m_hist;
    std::size_t m_head = 0;
    std::size_t m_cap = 240;
};

} // namespace okay
