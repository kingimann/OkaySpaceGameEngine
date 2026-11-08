#include "okay/Debug/DrawCallRecorder.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

namespace okay {

using namespace debugutil;

const char* DrawCallKindName(DrawCallKind k) {
    switch (k) {
        case DrawCallKind::Draw:          return "Draw";
        case DrawCallKind::DrawIndexed:   return "DrawIndexed";
        case DrawCallKind::DrawInstanced: return "DrawInstanced";
        case DrawCallKind::Dispatch:      return "Dispatch";
        case DrawCallKind::Clear:         return "Clear";
        case DrawCallKind::Copy:          return "Copy";
        case DrawCallKind::Other:         return "Other";
    }
    return "Other";
}

namespace {
std::atomic<IFrameDebugHook*>& HookSlot() {
    static std::atomic<IFrameDebugHook*> p{nullptr};
    return p;
}
bool IsDrawLike(DrawCallKind k) {
    return k == DrawCallKind::Draw || k == DrawCallKind::DrawIndexed || k == DrawCallKind::DrawInstanced;
}
}

IFrameDebugHook* ActiveFrameDebugHook() { return HookSlot().load(std::memory_order_acquire); }
void SetActiveFrameDebugHook(IFrameDebugHook* hook) { HookSlot().store(hook, std::memory_order_release); }

DrawCallRecorder::DrawCallRecorder(std::size_t maxCalls) : m_max(maxCalls) {}

// ---- capture ---------------------------------------------------------------

void DrawCallRecorder::RequestCapture() {
    if (m_capturing.load(std::memory_order_acquire)) return;
    m_armed.store(true, std::memory_order_release);
}

void DrawCallRecorder::CancelCapture() {
    SpinGuard g(m_lock);
    m_armed.store(false, std::memory_order_release);
    if (m_capturing.exchange(false, std::memory_order_acq_rel)) {
        m_cap = DrawCapture();
        m_hasCapture = false;
        m_break.store(-1, std::memory_order_relaxed);
    }
}

void DrawCallRecorder::ClearCapture() {
    SpinGuard g(m_lock);
    m_cap = DrawCapture();
    m_hasCapture = false;
    m_break.store(-1, std::memory_order_relaxed);
}

DrawCallRecorder::Status DrawCallRecorder::GetStatus() const {
    if (m_capturing.load(std::memory_order_acquire)) return Status::Capturing;
    if (m_armed.load(std::memory_order_acquire)) return Status::Armed;
    SpinGuard g(m_lock);
    return m_hasCapture ? Status::Captured : Status::Idle;
}

void DrawCallRecorder::SetMaxCalls(std::size_t n) {
    SpinGuard g(m_lock);
    m_max = n;
}

void DrawCallRecorder::BeginFrame(std::uint64_t frameNumber) {
    if (!m_armed.load(std::memory_order_acquire)) return;
    SpinGuard g(m_lock);
    m_armed.store(false, std::memory_order_release);
    m_cap = DrawCapture();
    m_cap.frame = frameNumber;
    m_hasCapture = false;
    m_break.store(-1, std::memory_order_relaxed);
    m_capturing.store(true, std::memory_order_release);
}

void DrawCallRecorder::EndFrame() {
    if (!m_capturing.load(std::memory_order_acquire)) return;
    SpinGuard g(m_lock);
    m_hasCapture = true;
    m_capturing.store(false, std::memory_order_release);
}

int DrawCallRecorder::Record(const DrawCallRecord& call) {
    if (!m_capturing.load(std::memory_order_acquire)) return -1;
    SpinGuard g(m_lock);
    if (!m_capturing.load(std::memory_order_relaxed)) return -1;
    if (m_cap.calls.size() >= m_max) { m_cap.truncated = true; return -1; }
    DrawCallRecord r = call;
    r.order = (std::uint32_t)m_cap.calls.size();
    m_cap.calls.push_back(std::move(r));
    return (int)m_cap.calls.size() - 1;
}

bool DrawCallRecorder::ShouldSkipDraw(std::uint32_t order) const {
    if (!m_replaying.load(std::memory_order_acquire)) return false;
    int b = m_break.load(std::memory_order_relaxed);
    return b >= 0 && (long long)order > (long long)b;
}

// ---- reading ---------------------------------------------------------------

std::size_t DrawCallRecorder::Count() const {
    SpinGuard g(m_lock);
    return m_hasCapture ? m_cap.calls.size() : 0;
}

bool DrawCallRecorder::Get(std::size_t i, DrawCallRecord& out) const {
    SpinGuard g(m_lock);
    if (!m_hasCapture || i >= m_cap.calls.size()) return false;
    out = m_cap.calls[i];
    return true;
}

DrawCapture DrawCallRecorder::Snapshot() const {
    SpinGuard g(m_lock);
    return m_hasCapture ? m_cap : DrawCapture();
}

std::uint64_t DrawCallRecorder::CapturedFrame() const {
    SpinGuard g(m_lock);
    return m_hasCapture ? m_cap.frame : 0;
}

bool DrawCallRecorder::Truncated() const {
    SpinGuard g(m_lock);
    return m_hasCapture && m_cap.truncated;
}

// ---- step through ----------------------------------------------------------

int DrawCallRecorder::ClampBreakLocked(int n) const {
    if (n < 0) return -1;
    int count = m_hasCapture ? (int)m_cap.calls.size() : 0;
    if (count == 0) return -1;
    return n > count - 1 ? count - 1 : n;
}

std::size_t DrawCallRecorder::ExecutedLocked() const {
    std::size_t count = m_hasCapture ? m_cap.calls.size() : 0;
    int b = m_break.load(std::memory_order_relaxed);
    if (b < 0) return count;
    std::size_t e = (std::size_t)b + 1;
    return e < count ? e : count;
}

void DrawCallRecorder::SetBreakAfter(int n) {
    SpinGuard g(m_lock);
    m_break.store(ClampBreakLocked(n), std::memory_order_relaxed);
}

int DrawCallRecorder::BreakAfter() const { return m_break.load(std::memory_order_relaxed); }

bool DrawCallRecorder::StepForward() {
    SpinGuard g(m_lock);
    int b = m_break.load(std::memory_order_relaxed);
    int count = m_hasCapture ? (int)m_cap.calls.size() : 0;
    if (b < 0 || b >= count - 1) return false;
    m_break.store(b + 1, std::memory_order_relaxed);
    return true;
}

bool DrawCallRecorder::StepBack() {
    SpinGuard g(m_lock);
    int b = m_break.load(std::memory_order_relaxed);
    int count = m_hasCapture ? (int)m_cap.calls.size() : 0;
    if (count < 2) return false;
    if (b < 0) { m_break.store(count - 2, std::memory_order_relaxed); return true; }
    if (b == 0) return false;
    m_break.store(b - 1, std::memory_order_relaxed);
    return true;
}

std::size_t DrawCallRecorder::ExecutedCount() const {
    SpinGuard g(m_lock);
    return ExecutedLocked();
}

int DrawCallRecorder::CurrentIndex() const {
    SpinGuard g(m_lock);
    int count = m_hasCapture ? (int)m_cap.calls.size() : 0;
    if (count == 0) return -1;
    int b = m_break.load(std::memory_order_relaxed);
    return b < 0 ? count - 1 : (b < count ? b : count - 1);
}

void DrawCallRecorder::BeginReplay() { m_replaying.store(true, std::memory_order_release); }
void DrawCallRecorder::EndReplay() { m_replaying.store(false, std::memory_order_release); }

// ---- filter, summary, export -----------------------------------------------

bool DrawCallRecorder::Matches(const DrawCallRecord& c, const DrawCallFilter& f, std::size_t executed) const {
    if (!(f.kindMask & DrawCallKindBit(c.kind))) return false;
    if (f.executedOnly && c.order >= executed) return false;
    if (!f.pass.empty() && c.pass != f.pass) return false;
    if (!f.material.empty() && c.material != f.material) return false;
    if (!f.mesh.empty() && c.mesh != f.mesh) return false;
    if (!f.shader.empty() && c.shaderState.shader != f.shader) return false;
    if (c.triangleCount < f.minTriangles) return false;
    if (!f.text.empty()) {
        const std::string* fields[] = {&c.name, &c.pass, &c.objectPath, &c.material, &c.mesh, &c.shaderState.shader, &c.renderTarget};
        bool any = false;
        for (const std::string* s : fields) if (Contains(*s, f.text, f.caseSensitive)) { any = true; break; }
        if (!any) return false;
    }
    return true;
}

std::vector<std::uint32_t> DrawCallRecorder::Filter(const DrawCallFilter& f) const {
    SpinGuard g(m_lock);
    std::vector<std::uint32_t> out;
    if (!m_hasCapture) return out;
    std::size_t executed = ExecutedLocked();
    for (const DrawCallRecord& c : m_cap.calls) if (Matches(c, f, executed)) out.push_back(c.order);
    return out;
}

DrawCallSummary DrawCallRecorder::Summarize(bool executedOnly) const {
    SpinGuard g(m_lock);
    DrawCallSummary s;
    if (!m_hasCapture) return s;
    std::size_t limit = executedOnly ? ExecutedLocked() : m_cap.calls.size();
    std::set<std::string> mats, meshes, shaders;
    bool haveMat = false, haveShader = false;
    std::string prevMat, prevShader, prevRt;
    for (std::size_t i = 0; i < limit; ++i) {
        const DrawCallRecord& c = m_cap.calls[i];
        ++s.calls;
        if (c.renderTarget != prevRt) { ++s.renderTargetSwitches; prevRt = c.renderTarget; }
        if (c.kind == DrawCallKind::Dispatch) { ++s.dispatches; continue; }
        if (!IsDrawLike(c.kind)) continue;
        ++s.draws;
        s.triangles += c.triangleCount;
        s.vertices += (std::uint64_t)c.vertexCount * (c.instanceCount ? c.instanceCount : 1);
        s.instances += c.instanceCount;
        if (!c.material.empty()) {
            mats.insert(c.material);
            if (!haveMat || c.material != prevMat) { ++s.materialSwitches; prevMat = c.material; haveMat = true; }
        }
        if (!c.shaderState.shader.empty()) {
            shaders.insert(c.shaderState.shader);
            if (!haveShader || c.shaderState.shader != prevShader) { ++s.shaderSwitches; prevShader = c.shaderState.shader; haveShader = true; }
        }
        if (!c.mesh.empty()) meshes.insert(c.mesh);
    }
    s.uniqueMaterials = mats.size(); s.uniqueMeshes = meshes.size(); s.uniqueShaders = shaders.size();
    return s;
}

std::string DrawCallSummary::ToText() const {
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "%zu call(s): %zu draw, %zu dispatch; %llu tris, %llu verts, %llu instances; switches: material %zu, shader %zu, target %zu; "
                  "unique: %zu materials, %zu meshes, %zu shaders",
                  calls, draws, dispatches, (unsigned long long)triangles, (unsigned long long)vertices, (unsigned long long)instances,
                  materialSwitches, shaderSwitches, renderTargetSwitches, uniqueMaterials, uniqueMeshes, uniqueShaders);
    return buf;
}

static void AppendCallJson(std::string& out, const DrawCallRecord& c) {
    out += "{\"order\":" + std::to_string(c.order) + ",\"kind\":\"" + DrawCallKindName(c.kind) + "\",\"name\":";
    AppendJsonString(out, c.name);
    out += ",\"pass\":"; AppendJsonString(out, c.pass);
    out += ",\"object\":"; AppendJsonString(out, c.objectPath);
    out += ",\"material\":"; AppendJsonString(out, c.material);
    out += ",\"mesh\":"; AppendJsonString(out, c.mesh);
    out += ",\"renderTarget\":"; AppendJsonString(out, c.renderTarget);
    out += ",\"shader\":"; AppendJsonString(out, c.shaderState.shader);
    out += ",\"state\":{\"blend\":"; AppendJsonString(out, c.shaderState.blend);
    out += std::string(",\"depthTest\":") + (c.shaderState.depthTest ? "true" : "false")
         + ",\"depthWrite\":" + (c.shaderState.depthWrite ? "true" : "false")
         + ",\"cullBack\":" + (c.shaderState.cullBack ? "true" : "false")
         + ",\"wireframe\":" + (c.shaderState.wireframe ? "true" : "false") + ",\"extra\":{";
    bool first = true;
    for (const auto& kv : c.shaderState.extra) {
        if (!first) out += ',';
        first = false;
        AppendJsonString(out, kv.first); out += ':'; AppendJsonString(out, kv.second);
    }
    out += "}},\"viewport\":{\"x\":" + std::to_string(c.viewport.x) + ",\"y\":" + std::to_string(c.viewport.y)
         + ",\"width\":" + std::to_string(c.viewport.width) + ",\"height\":" + std::to_string(c.viewport.height)
         + "},\"vertexCount\":" + std::to_string(c.vertexCount) + ",\"indexCount\":" + std::to_string(c.indexCount)
         + ",\"instanceCount\":" + std::to_string(c.instanceCount) + ",\"triangleCount\":" + std::to_string(c.triangleCount)
         + ",\"groups\":[" + std::to_string(c.groupsX) + "," + std::to_string(c.groupsY) + "," + std::to_string(c.groupsZ) + "]}";
}

std::string DrawCallRecorder::ExportJson(const DrawCallFilter* filter) const {
    DrawCapture cap = Snapshot();
    int brk = BreakAfter();
    std::size_t executed = ExecutedCount();
    DrawCallSummary sum = Summarize(false);
    std::string out = "{\"frame\":" + std::to_string(cap.frame) + ",\"truncated\":" + (cap.truncated ? "true" : "false")
                    + ",\"callCount\":" + std::to_string(cap.calls.size()) + ",\"breakAfter\":" + std::to_string(brk)
                    + ",\"summary\":{\"draws\":" + std::to_string(sum.draws) + ",\"dispatches\":" + std::to_string(sum.dispatches)
                    + ",\"triangles\":" + std::to_string(sum.triangles) + ",\"vertices\":" + std::to_string(sum.vertices)
                    + ",\"instances\":" + std::to_string(sum.instances) + ",\"materialSwitches\":" + std::to_string(sum.materialSwitches)
                    + ",\"shaderSwitches\":" + std::to_string(sum.shaderSwitches) + ",\"renderTargetSwitches\":" + std::to_string(sum.renderTargetSwitches)
                    + ",\"uniqueMaterials\":" + std::to_string(sum.uniqueMaterials) + ",\"uniqueMeshes\":" + std::to_string(sum.uniqueMeshes)
                    + ",\"uniqueShaders\":" + std::to_string(sum.uniqueShaders) + "},\"calls\":[";
    bool first = true;
    for (const DrawCallRecord& c : cap.calls) {
        if (filter && !Matches(c, *filter, executed)) continue;
        if (!first) out += ',';
        first = false;
        AppendCallJson(out, c);
    }
    out += "]}";
    return out;
}

bool DrawCallRecorder::ExportJsonToFile(const std::string& path, const DrawCallFilter* filter) const {
    return WriteTextFile(path, ExportJson(filter));
}

std::string DrawCallRecorder::ExportText(const DrawCallFilter* filter) const {
    DrawCapture cap = Snapshot();
    std::size_t executed = ExecutedCount();
    std::string out = "Frame " + std::to_string(cap.frame) + ": " + std::to_string(cap.calls.size()) + " call(s)"
                    + (cap.truncated ? " (truncated)" : "") + "\n";
    for (const DrawCallRecord& c : cap.calls) {
        if (filter && !Matches(c, *filter, executed)) continue;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%4u %-13s %llu tris x%u", c.order, DrawCallKindName(c.kind),
                      (unsigned long long)c.triangleCount, c.instanceCount);
        out += buf;
        out += "  " + c.name;
        if (!c.pass.empty()) out += "  pass=" + c.pass;
        if (!c.objectPath.empty()) out += "  obj=" + c.objectPath;
        if (!c.material.empty()) out += "  mat=" + c.material;
        if (!c.mesh.empty()) out += "  mesh=" + c.mesh;
        if (!c.shaderState.shader.empty()) out += "  shader=" + c.shaderState.shader;
        out += '\n';
    }
    return out;
}

} // namespace okay
