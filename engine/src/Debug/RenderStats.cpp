#include "okay/Debug/RenderStats.hpp"
#include "okay/Core/Profiler.hpp"

#include <cstdio>

namespace okay {

using namespace debugutil;

std::uint64_t GpuFrameStats::EstimateTextureBytes(int width, int height, int bytesPerPixel, bool mipmaps, int layers) {
    if (width <= 0 || height <= 0 || bytesPerPixel <= 0 || layers <= 0) return 0;
    std::uint64_t base = (std::uint64_t)width * (std::uint64_t)height * (std::uint64_t)bytesPerPixel * (std::uint64_t)layers;
    return mipmaps ? base + base / 3 : base;
}

std::string GpuFrameStats::ToText() const {
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "Frame %llu: %u draw calls, %llu tris, %llu verts, %llu instances; switches: material %u, shader %u, texture %u, target %u; "
                  "textures ~%s, meshes ~%s, gpu %.3f ms",
                  (unsigned long long)frame, drawCalls, (unsigned long long)triangles, (unsigned long long)vertices,
                  (unsigned long long)instances, materialSwitches, shaderSwitches, textureSwitches, renderTargetSwitches,
                  FormatBytes((long long)textureBytes).c_str(), FormatBytes((long long)meshBytes).c_str(), gpuMs);
    return buf;
}

std::string GpuFrameStats::ToJson() const {
    std::string out = "{\"frame\":" + std::to_string(frame) + ",\"drawCalls\":" + std::to_string(drawCalls)
        + ",\"dispatches\":" + std::to_string(dispatches) + ",\"triangles\":" + std::to_string(triangles)
        + ",\"vertices\":" + std::to_string(vertices) + ",\"instances\":" + std::to_string(instances)
        + ",\"materialSwitches\":" + std::to_string(materialSwitches) + ",\"shaderSwitches\":" + std::to_string(shaderSwitches)
        + ",\"textureSwitches\":" + std::to_string(textureSwitches) + ",\"renderTargetSwitches\":" + std::to_string(renderTargetSwitches)
        + ",\"textureBytes\":" + std::to_string(textureBytes) + ",\"meshBytes\":" + std::to_string(meshBytes) + ",\"gpuMs\":";
    AppendNum(out, gpuMs);
    out += '}';
    return out;
}

RenderStatsCollector::RenderStatsCollector(std::size_t historyFrames) : m_cap(historyFrames < 1 ? 1 : historyFrames) {}

RenderStatsCollector& RenderStatsCollector::Global() {
    static RenderStatsCollector c;
    return c;
}

void RenderStatsCollector::BeginFrame() {
    m_draws = 0; m_dispatches = 0; m_matSw = 0; m_shaderSw = 0; m_texSw = 0; m_rtSw = 0;
    m_tris = 0; m_verts = 0; m_inst = 0;
    m_boundMat = kNone; m_boundShader = kNone; m_boundTex = kNone; m_boundRt = kNone;
    m_gpuMs = 0.0;
    m_frame.fetch_add(1, std::memory_order_relaxed);
}

GpuFrameStats RenderStatsCollector::Current() const {
    GpuFrameStats s;
    s.frame = m_frame.load(std::memory_order_relaxed);
    s.drawCalls = m_draws.load(std::memory_order_relaxed);
    s.dispatches = m_dispatches.load(std::memory_order_relaxed);
    s.triangles = m_tris.load(std::memory_order_relaxed);
    s.vertices = m_verts.load(std::memory_order_relaxed);
    s.instances = m_inst.load(std::memory_order_relaxed);
    s.materialSwitches = m_matSw.load(std::memory_order_relaxed);
    s.shaderSwitches = m_shaderSw.load(std::memory_order_relaxed);
    s.textureSwitches = m_texSw.load(std::memory_order_relaxed);
    s.renderTargetSwitches = m_rtSw.load(std::memory_order_relaxed);
    s.textureBytes = m_texBytes.load(std::memory_order_relaxed);
    s.meshBytes = m_meshBytes.load(std::memory_order_relaxed);
    s.gpuMs = m_gpuMs.load(std::memory_order_relaxed);
    return s;
}

void RenderStatsCollector::EndFrame() {
    GpuFrameStats s = Current();
    SpinGuard g(m_lock);
    m_last = s;
    if (m_hist.size() < m_cap) m_hist.push_back(s);
    else { m_hist[m_head] = s; m_head = (m_head + 1) % m_hist.size(); }
}

void RenderStatsCollector::DrawCall(std::uint64_t triangles, std::uint64_t vertices, std::uint32_t instances) {
    m_draws.fetch_add(1, std::memory_order_relaxed);
    m_tris.fetch_add(triangles * (instances ? instances : 1), std::memory_order_relaxed);
    m_verts.fetch_add(vertices * (instances ? instances : 1), std::memory_order_relaxed);
    m_inst.fetch_add(instances, std::memory_order_relaxed);
}

void RenderStatsCollector::Dispatch() { m_dispatches.fetch_add(1, std::memory_order_relaxed); }

void RenderStatsCollector::BindMaterial(std::uint64_t id)     { if (Switch(m_boundMat, id))    m_matSw.fetch_add(1, std::memory_order_relaxed); }
void RenderStatsCollector::BindShader(std::uint64_t id)       { if (Switch(m_boundShader, id)) m_shaderSw.fetch_add(1, std::memory_order_relaxed); }
void RenderStatsCollector::BindTexture(std::uint64_t id)      { if (Switch(m_boundTex, id))    m_texSw.fetch_add(1, std::memory_order_relaxed); }
void RenderStatsCollector::BindRenderTarget(std::uint64_t id) { if (Switch(m_boundRt, id))     m_rtSw.fetch_add(1, std::memory_order_relaxed); }

static void SubClamped(std::atomic<std::uint64_t>& a, std::uint64_t v) {
    std::uint64_t cur = a.load(std::memory_order_relaxed);
    while (!a.compare_exchange_weak(cur, cur > v ? cur - v : 0, std::memory_order_relaxed)) {}
}

void RenderStatsCollector::TextureCreated(std::uint64_t bytes)   { m_texBytes.fetch_add(bytes, std::memory_order_relaxed); }
void RenderStatsCollector::TextureDestroyed(std::uint64_t bytes) { SubClamped(m_texBytes, bytes); }
void RenderStatsCollector::MeshCreated(std::uint64_t bytes)      { m_meshBytes.fetch_add(bytes, std::memory_order_relaxed); }
void RenderStatsCollector::MeshDestroyed(std::uint64_t bytes)    { SubClamped(m_meshBytes, bytes); }

GpuFrameStats RenderStatsCollector::Last() const {
    SpinGuard g(m_lock);
    return m_last;
}

std::vector<GpuFrameStats> RenderStatsCollector::History(std::size_t maxFrames) const {
    SpinGuard g(m_lock);
    std::vector<GpuFrameStats> out;
    std::size_t n = m_hist.size();
    std::size_t take = (maxFrames == 0 || maxFrames > n) ? n : maxFrames;
    out.reserve(take);
    for (std::size_t i = n - take; i < n; ++i) out.push_back(m_hist[(m_head + i) % n]);
    return out;
}

void RenderStatsCollector::Reset() {
    BeginFrame();
    m_frame = 0;
    m_texBytes = 0; m_meshBytes = 0;
    SpinGuard g(m_lock);
    m_last = GpuFrameStats();
    m_hist.clear(); m_head = 0;
}

void RenderStatsCollector::ApplyToProfiler(Profiler& p, int particles) const {
    GpuFrameStats s = Last();
    p.Stat3((int)s.instances, (int)s.triangles, particles);
}

} // namespace okay
