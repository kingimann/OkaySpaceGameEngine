#include "okay/Render/Culling.hpp"
#include <chrono>
#include "okay/Core/FrameOverlay.hpp"
#include "okay/Render/TextureAsset.hpp"   // textures::OverlayLine
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Time.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace okay {

namespace {

// printf for one number, with a dot whatever the process locale (an overlay is compared in tests, and a
// decimal comma in one panel and a dot in the other would defeat "the same text everywhere").
std::string Num(const char* fmt, double v) {
    char b[64];
    std::snprintf(b, sizeof b, fmt, v);
    for (char* p = b; *p; ++p) if (*p == ',') *p = '.';
    return b;
}

constexpr std::size_t kMaxGraphPoints = 4096;

}  // namespace

FrameOverlayState FrameOverlay::StateOf(const GameLoop& loop) {
    FrameOverlayState s;
    s.timeScale = Time::BaseTimeScale();
    s.hitStop = Time::HitStopActive();
    s.hitStopSeconds = Time::HitStopRemaining();
    s.ramping = Time::IsRamping();
    s.rampTarget = Time::RampTarget();
    s.debugTimeScale = loop.DebugTimeScale();
    s.captureFps = loop.CaptureFps();
    s.runFramesLeft = loop.RunFramesRemaining();
    s.stepsLeft = loop.StepsRemaining();
    s.clampedFrames = loop.ClampedFrames();
    s.lostRealSeconds = loop.LostRealTime();
    s.droppedFixedSeconds = loop.DroppedFixedTime();
    s.pausedBy = Game::PauseReasons();
    return s;
}

std::vector<std::string> FrameOverlay::Format(const FrameStatsSnapshot& st, const FrameOverlayState& s) {
    std::vector<std::string> out;
    if (st.frames == 0) {
        out.push_back("no frames yet");
    } else {
        out.push_back(Num("%.1f", st.fps) + " fps  " + Num("%.2f", st.avgMs) + " ms avg");
        out.push_back("min " + Num("%.2f", st.minMs) + "  max " + Num("%.2f", st.worstMs) + "  p99 " + Num("%.2f", st.p99Ms) + " ms");
        out.push_back("1% low " + Num("%.1f", st.low1PercentFps) + " fps  0.1% low " + Num("%.1f", st.low01PercentFps) + " fps");
        if (st.totalHitches > 0)
            out.push_back("hitches " + std::to_string(st.hitches) + " (" + std::to_string((unsigned long long)st.totalHitches) +
                          " total)  worst " + Num("%.0f", st.worstMs) + " ms");
    }
    if (s.clampedFrames > 0)
        out.push_back("dropped " + std::to_string(s.clampedFrames) + (s.clampedFrames == 1 ? " frame (" : " frames (") +
                      Num("%.2f", s.lostRealSeconds) + " s lost)");
    if (s.droppedFixedSeconds > 0.0)
        out.push_back("fixed steps dropped " + Num("%.2f", s.droppedFixedSeconds) + " s");

    std::string time = "time x" + Num("%.2f", s.timeScale);
    if (s.hitStop) time += "  hit stop " + Num("%.2f", s.hitStopSeconds) + " s";
    if (s.ramping) time += "  ramp to x" + Num("%.2f", s.rampTarget);
    if (s.debugTimeScale != 1.0f) time += "  debug x" + Num("%.2f", s.debugTimeScale);
    if (s.captureFps > 0.0f) time += "  capture " + Num("%.0f", s.captureFps) + " fps";
    if (s.runFramesLeft > 0) time += "  run " + std::to_string(s.runFramesLeft) + " more";
    if (s.stepsLeft > 0) time += "  step " + std::to_string(s.stepsLeft);
    out.push_back(time);

    if (!s.pausedBy.empty()) {
        std::string p = "paused by: ";
        for (std::size_t i = 0; i < s.pausedBy.size(); ++i) { if (i) p += ", "; p += s.pausedBy[i]; }
        out.push_back(p);
    }
    return out;
}

VoxelOverlayStats& FrameOverlay::Voxel() { static VoxelOverlayStats v; return v; }
std::string& FrameOverlay::RendererName() { static std::string n; return n; }

double FrameOverlay::NowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::vector<std::string> FrameOverlay::Lines(const GameLoop& loop) {
    std::vector<std::string> out = Format(loop.Stats().Snapshot(), StateOf(loop));
    if (!RendererName().empty()) out.push_back("renderer: " + RendererName());
    if (textures::GetStats().count > 0) out.push_back(textures::OverlayLine());   // the shared texture library: count, MB, budget
    // Where the time goes in a voxel scene: only while a voxel terrain is running (it reported within the last second).
    const VoxelOverlayStats& v = Voxel();
    if (NowSeconds() - v.seenAt < 1.0) {
        out.push_back("voxel: " + std::to_string(v.terrains) + (v.terrains == 1 ? " terrain, " : " terrains, ") + std::to_string((long long)v.triangles) +
                      " triangles, " + (v.chunkObjects > 0 ? std::to_string(v.chunkObjects) + " chunk objects of " + std::to_string(v.chunksTotal) : "one combined mesh (" + std::to_string(v.chunksTotal) + " chunks)"));
        out.push_back("voxel: terrain " + Num("%.2f", v.updateMs) + " ms/frame, digger " + Num("%.2f", v.digMs) + " ms/frame, last rebuild " +
                      Num("%.2f", v.lastMeshMs) + " ms (" + std::to_string(v.lastChunksRemeshed) + " chunks)" + (v.oldRemesh ? "  OLD REMESH ON" : ""));
        const CullCounters& c = LastCullCounters();
        if (c.drawn + c.culled > 0)
            out.push_back("draw: " + std::to_string(c.drawn) + " drawn, " + std::to_string(c.culled) + " frustum-culled, " + std::to_string(c.occluded) +
                          " occluded, " + std::to_string(c.batches) + " batches");
    }
    return out;
}

std::string FrameOverlay::Text(const std::vector<std::string>& lines) {
    std::string t;
    for (std::size_t i = 0; i < lines.size(); ++i) { if (i) t += '\n'; t += lines[i]; }
    return t;
}

std::string FrameOverlay::FpsLabel(const FrameStats& stats, std::size_t recentFrames) {
    const double fps = stats.AverageFps(recentFrames);
    if (stats.Empty() || !(fps > 0.0)) return "FPS --";
    return "FPS " + std::to_string((long long)(fps > 1.0e9 ? 1.0e9 : fps + 0.5));
}

std::string FrameOverlay::FpsLabel(const GameLoop& loop, std::size_t recentFrames) { return FpsLabel(loop.Stats(), recentFrames); }

static std::vector<float> Recent(const FrameStats& stats, std::size_t count, bool fps) {
    if (count > kMaxGraphPoints) count = kMaxGraphPoints;
    std::vector<float> out(count, 0.0f);
    const std::vector<double> v = stats.Values();   // oldest first
    const std::size_t take = std::min(count, v.size());
    for (std::size_t i = 0; i < take; ++i) {
        const double ms = v[v.size() - take + i];
        out[count - take + i] = fps ? (ms > 0.0 ? (float)(1000.0 / ms) : 0.0f) : (float)ms;
    }
    return out;
}

std::vector<float> FrameOverlay::RecentFps(const FrameStats& stats, std::size_t count) { return Recent(stats, count, true); }
std::vector<float> FrameOverlay::RecentMs(const FrameStats& stats, std::size_t count) { return Recent(stats, count, false); }

}  // namespace okay
