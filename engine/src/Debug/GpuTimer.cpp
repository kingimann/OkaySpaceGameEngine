#include "okay/Debug/GpuTimer.hpp"

#include <atomic>
#include <chrono>

namespace okay {

namespace {
std::atomic<GpuTimer*>& ActiveSlot() {
    static std::atomic<GpuTimer*> p{nullptr};
    return p;
}
}

GpuTimer* ActiveGpuTimer() { return ActiveSlot().load(std::memory_order_acquire); }
void SetActiveGpuTimer(GpuTimer* timer) { ActiveSlot().store(timer, std::memory_order_release); }

CpuFenceGpuTimer::CpuFenceGpuTimer(Fence fence, Clock clock)
    : m_fence(std::move(fence)), m_clock(std::move(clock)) {}

double CpuFenceGpuTimer::Now() const {
    if (m_clock) return m_clock();
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void CpuFenceGpuTimer::BeginFrame() {
    if (!m_enabled) return;
    if (m_fence) m_fence();          // start from an idle GPU so the frame is not charged for the last one
    ++m_frame;
    m_cur = GpuFrameTiming();
    m_cur.frame = m_frame;
    m_open.clear();
    m_frameStart = Now();
    m_inFrame = true;
}

void CpuFenceGpuTimer::BeginScope(const char* name) {
    if (!m_enabled || !m_inFrame) return;
    if (m_fence) m_fence();
    GpuScopeTime s;
    s.name = name ? name : "";
    s.depth = (int)m_open.size();
    m_cur.scopes.push_back(std::move(s));
    Open o;
    o.index = m_cur.scopes.size() - 1;
    o.start = Now();
    m_open.push_back(o);
}

void CpuFenceGpuTimer::EndScope() {
    if (!m_enabled || !m_inFrame || m_open.empty()) return;
    if (m_fence) m_fence();
    Open o = m_open.back();
    m_open.pop_back();
    double ms = Now() - o.start;
    m_cur.scopes[o.index].ms = ms > 0.0 ? ms : 0.0;
}

void CpuFenceGpuTimer::EndFrame() {
    if (!m_enabled || !m_inFrame) return;
    while (!m_open.empty()) EndScope();   // a scope left open still reports
    if (m_fence) m_fence();
    double ms = Now() - m_frameStart;
    m_cur.totalMs = ms > 0.0 ? ms : 0.0;
    m_cur.fromHardware = false;
    m_ready.push_back(std::move(m_cur));
    m_cur = GpuFrameTiming();
    if (m_ready.size() > 8) m_ready.erase(m_ready.begin());
    m_inFrame = false;
}

bool CpuFenceGpuTimer::Poll(GpuFrameTiming& out) {
    if (m_ready.empty()) return false;
    out = std::move(m_ready.front());
    m_ready.erase(m_ready.begin());
    return true;
}

} // namespace okay
