#pragma once
// Frame debugger data model (feature map 23.3): captures one frame's list of draw
// and dispatch calls (what was drawn, with which material, mesh and state), lets a
// window filter it and step through it, and exports it as JSON or text. The
// renderers are not modified: they would report through the small IFrameDebugHook
// interface below, documented in docs/debug_tools.md.
//
//   Editor / UI side                           Renderer side (additive, a few lines)
//   ----------------                           -------------------------------------
//   DrawCallRecorder rec;                      auto* hook = ActiveFrameDebugHook();
//   SetActiveFrameDebugHook(&rec);             if (hook) hook->BeginFrame(frameNumber);
//   rec.RequestCapture();   // next frame      ...
//   ...                                        if (hook && hook->IsCapturing()) {
//   rec.Count(); rec.Get(i, call);                 DrawCallRecord r; r.name = ...; hook->Record(r);
//   rec.SetBreakAfter(12); rec.StepForward();  }
//   rec.ExportJson();                          if (hook) hook->EndFrame();
//
// Step-through works by replay: with BeginReplay() on, a renderer that asks
// ShouldSkipDraw(order) before each draw skips every call after the break, so the
// frame is drawn only up to the selected call. Recording and replay are thread
// safe (a spin lock around the call list); IsCapturing() is a single atomic load,
// so the renderer's fast path costs nothing when no capture is wanted.
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "okay/Debug/DebugUtil.hpp"

namespace okay {

enum class DrawCallKind { Draw, DrawIndexed, DrawInstanced, Dispatch, Clear, Copy, Other };
const char* DrawCallKindName(DrawCallKind k);
/// Bit for DrawCallFilter::kindMask.
inline unsigned DrawCallKindBit(DrawCallKind k) { return 1u << static_cast<unsigned>(k); }

struct DrawViewport {
    int x = 0, y = 0, width = 0, height = 0;
};

/// The pipeline state worth showing next to a call.
struct DrawShaderState {
    std::string shader;             // program or shader name
    std::string blend;              // "opaque", "alpha", "additive", ...
    bool depthTest = true;
    bool depthWrite = true;
    bool cullBack = true;
    bool wireframe = false;
    std::vector<std::pair<std::string, std::string>> extra;   // uniforms or other name/value pairs
};

struct DrawCallRecord {
    std::uint32_t order = 0;        // position in the frame; set by the recorder
    DrawCallKind kind = DrawCallKind::Draw;
    std::string name;               // label, e.g. "Crate"
    std::string pass;               // "Shadows", "Opaque", "Post", ...
    std::string objectPath;         // scene path of the object, "World/Props/Crate"
    std::string material;           // material or asset name
    std::string mesh;
    std::string renderTarget;       // empty = the backbuffer / main target
    DrawShaderState shaderState;
    DrawViewport viewport;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t instanceCount = 1;
    std::uint64_t triangleCount = 0;            // for the whole call (all instances)
    std::uint32_t groupsX = 0, groupsY = 0, groupsZ = 0;   // Dispatch
};

/// One captured frame.
struct DrawCapture {
    std::uint64_t frame = 0;
    bool truncated = false;         // more calls were reported than the recorder keeps
    std::vector<DrawCallRecord> calls;
};

struct DrawCallFilter {
    std::string text;               // substring of name, pass, object path, material, mesh, shader or render target
    bool caseSensitive = false;
    unsigned kindMask = ~0u;        // OR of DrawCallKindBit
    std::string pass;               // exact, empty = any
    std::string material;           // exact, empty = any
    std::string mesh;
    std::string shader;
    std::uint64_t minTriangles = 0;
    bool executedOnly = false;      // only calls up to the current break
};

struct DrawCallSummary {
    std::size_t calls = 0;
    std::size_t draws = 0;          // everything that is not a dispatch, clear or copy
    std::size_t dispatches = 0;
    std::uint64_t triangles = 0;
    std::uint64_t vertices = 0;
    std::uint64_t instances = 0;
    std::size_t materialSwitches = 0;     // a call whose material differs from the previous drawing call
    std::size_t shaderSwitches = 0;
    std::size_t renderTargetSwitches = 0;
    std::size_t uniqueMaterials = 0, uniqueMeshes = 0, uniqueShaders = 0;
    std::string ToText() const;
};

/// What a renderer sees. DrawCallRecorder implements it; install one with
/// SetActiveFrameDebugHook. Every method may be called from the render thread.
class IFrameDebugHook {
public:
    virtual ~IFrameDebugHook() = default;
    /// First thing in a frame. A pending capture request starts here.
    virtual void BeginFrame(std::uint64_t frameNumber) = 0;
    /// Last thing in a frame. A running capture finishes here.
    virtual void EndFrame() = 0;
    /// Cheap check before building a DrawCallRecord (strings cost).
    virtual bool IsCapturing() const = 0;
    /// Report one call; ignored when not capturing. Returns its order, or -1.
    virtual int Record(const DrawCallRecord& call) = 0;
    /// During a replay: true if the draw with this order must be skipped.
    virtual bool ShouldSkipDraw(std::uint32_t order) const = 0;
};

/// The hook renderers report to; null (the default) = frame debugging off.
IFrameDebugHook* ActiveFrameDebugHook();
void SetActiveFrameDebugHook(IFrameDebugHook* hook);

class DrawCallRecorder final : public IFrameDebugHook {
public:
    enum class Status { Idle, Armed, Capturing, Captured };

    explicit DrawCallRecorder(std::size_t maxCalls = 100000);

    // ---- capture ----------------------------------------------------------
    /// Capture the next frame (from its BeginFrame to its EndFrame).
    void RequestCapture();
    /// Drop a pending request or a capture in progress.
    void CancelCapture();
    void ClearCapture();
    Status GetStatus() const;
    bool HasCapture() const { return GetStatus() == Status::Captured; }
    void SetMaxCalls(std::size_t n);

    // IFrameDebugHook
    void BeginFrame(std::uint64_t frameNumber) override;
    void EndFrame() override;
    bool IsCapturing() const override { return m_capturing.load(std::memory_order_acquire); }
    int Record(const DrawCallRecord& call) override;
    bool ShouldSkipDraw(std::uint32_t order) const override;

    // ---- reading ----------------------------------------------------------
    std::size_t Count() const;
    /// Copy of call i (0 = first drawn). False when out of range.
    bool Get(std::size_t i, DrawCallRecord& out) const;
    /// Copy of the whole capture.
    DrawCapture Snapshot() const;
    std::uint64_t CapturedFrame() const;
    bool Truncated() const;

    // ---- step through -----------------------------------------------------
    /// Break after call n: calls 0..n count as drawn, later ones as not yet. n < 0 clears the break;
    /// n past the end clamps to the last call.
    void SetBreakAfter(int n);
    void ClearBreak() { SetBreakAfter(-1); }
    /// -1 when there is no break.
    int BreakAfter() const;
    /// Move the break one call later. With no break set it does nothing. False if it could not move.
    bool StepForward();
    /// Move the break one call earlier (stops at call 0). With no break set it goes to the
    /// second to last call. False if it could not move.
    bool StepBack();
    void StepToStart() { SetBreakAfter(0); }
    void StepToEnd() { ClearBreak(); }
    /// Calls drawn at the current break (all of them with no break).
    std::size_t ExecutedCount() const;
    bool IsExecuted(std::size_t i) const { return i < ExecutedCount(); }
    /// Index of the call at the break, or of the last call; -1 for an empty capture.
    int CurrentIndex() const;
    /// While replaying, ShouldSkipDraw reports calls past the break (renderer side).
    void BeginReplay();
    void EndReplay();
    bool Replaying() const { return m_replaying.load(std::memory_order_acquire); }

    // ---- filter, summary, export -----------------------------------------
    /// Indices (orders) of the calls that pass the filter, in drawing order.
    std::vector<std::uint32_t> Filter(const DrawCallFilter& f) const;
    DrawCallSummary Summarize(bool executedOnly = false) const;
    /// JSON of the capture (optionally only the calls that pass `filter`); includes the summary.
    std::string ExportJson(const DrawCallFilter* filter = nullptr) const;
    bool ExportJsonToFile(const std::string& path, const DrawCallFilter* filter = nullptr) const;
    /// One line per call.
    std::string ExportText(const DrawCallFilter* filter = nullptr) const;

private:
    bool Matches(const DrawCallRecord& c, const DrawCallFilter& f, std::size_t executed) const;
    int ClampBreakLocked(int n) const;
    std::size_t ExecutedLocked() const;

    mutable debugutil::SpinLock m_lock;
    std::atomic<bool> m_armed{false}, m_capturing{false}, m_replaying{false};
    bool m_hasCapture = false;
    DrawCapture m_cap;
    std::size_t m_max = 100000;
    std::atomic<int> m_break{-1};
};

} // namespace okay
