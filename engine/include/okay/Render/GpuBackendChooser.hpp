#pragma once
// ---------------------------------------------------------------------------
// Which GPU backend to try, in which order. Pure logic: no GL, no Direct3D, no
// window, no Scene. It can be unit tested headless (tests/test_gpu_backend.cpp).
//
// Written from the behaviour of the two selection chains that already existed
// (player/main.cpp and the editor's Render3DTexture in editor/main.cpp), read off
// the code, not from any other project:
//   player : Direct3D 12 (only when opted in), then Direct3D 11, then OpenGL,
//            then software; 3 failed frames switch the GPU off for the run;
//            `gpu=0` in the build config skips every GPU backend.
//   editor : Direct3D 11 (Windows), then OpenGL, then software; 3 failed frames
//            rest the GPU for 5 seconds and then retry. The editor never offers
//            Direct3D 12.
// ChooseGpuBackends() reproduces both orders; the hosts now call it instead of
// repeating the if-chains. Software is always last and is the host's own
// fallback (the host skips it in the GPU loop).
//
// No <mutex>, <thread> or <condition_variable> here: the launcher, editor and
// player can reach this header (CLAUDE.md rule 21a).
// ---------------------------------------------------------------------------
#include "okay/Render/RendererFeatures.hpp"   // RenderBackend, kRenderBackendCount
#include <cstdint>
#include <string>

namespace okay {

/// What a backend can do. Static numbers read off the renderers (see
/// docs/render_backends.md). `maxMsaaSamples` is the highest sample count the
/// hosts ask for today (4), not a probed hardware limit.
struct GpuCapabilities {
    int  maxLights = 0;           // lights the shader loops over; 0 = not a fixed GPU array
    int  maxMsaaSamples = 1;      // 1 = no MSAA
    bool shadows = false;         // sun shadow map
    bool localShadows = false;    // spot / point / area shadow tiles
    bool gpuSkinning = false;     // bone palette in the vertex shader
    bool postChain = false;       // exposure / bloom / grading / FXAA chain on the GPU
    bool directPresent = false;   // can draw straight into a host texture (no CPU read-back)
    bool compute = false;         // compute shaders in use (none of the four use them yet)
};

/// What a host needs. A backend that does not meet every non-default field is
/// left out of the order. The all-default value asks for nothing.
struct GpuRequirements {
    int  minLights = 0;
    int  minMsaaSamples = 0;
    bool shadows = false;
    bool localShadows = false;
    bool gpuSkinning = false;
    bool postChain = false;
    bool directPresent = false;
    bool compute = false;
};

inline bool GpuMeetsRequirements(const GpuCapabilities& c, const GpuRequirements& r) {
    return c.maxLights >= r.minLights && c.maxMsaaSamples >= r.minMsaaSamples &&
           (!r.shadows || c.shadows) && (!r.localShadows || c.localShadows) &&
           (!r.gpuSkinning || c.gpuSkinning) && (!r.postChain || c.postChain) &&
           (!r.directPresent || c.directPresent) && (!r.compute || c.compute);
}

/// MSAA sample counts are powers of two (1, 2, 4, 8, 16, 32, 64). Returns the
/// largest supported count that is <= `wanted`, from a Vulkan-style bit mask
/// (the flag for N samples is the value N: 4 samples = 0x4). 1 when nothing fits.
inline int GpuClampMsaaSamples(int wanted, std::uint32_t supportedMask) {
    int best = 1;
    for (int bit = 0; bit < 7; ++bit) {
        const int n = 1 << bit;
        if (n > wanted) break;
        if (supportedMask & static_cast<std::uint32_t>(n)) best = n;   // the flag value equals the count (4 samples = 0x4)
    }
    return best;
}

/// Static capability table for the four renderers that exist. Software is the
/// CPU rasteriser (sun cascades, no local shadows, no GPU post chain; its light
/// count was not audited and reads 0 = not a fixed array).
inline const GpuCapabilities& GpuBackendCapabilities(RenderBackend b) {
    static const GpuCapabilities kTable[kRenderBackendCount] = {
        /* Software   */ {0, 1, true,  false, false, false, false, false},
        /* OpenGL     */ {16, 4, true, true,  true,  true,  false, false},
        /* Direct3D11 */ {16, 4, true, true,  true,  true,  true,  false},
        /* Direct3D12 */ {16, 4, true, false, false, false, false, false},
    };
    const int i = static_cast<int>(b);
    return kTable[(i >= 0 && i < kRenderBackendCount) ? i : 0];
}

/// Short stable id ("software", "opengl", "d3d11", "d3d12") for logs and prefs.
inline const char* GpuBackendId(RenderBackend b) {
    switch (b) {
        case RenderBackend::Software:   return "software";
        case RenderBackend::OpenGL:     return "opengl";
        case RenderBackend::Direct3D11: return "d3d11";
        case RenderBackend::Direct3D12: return "d3d12";
    }
    return "software";
}

/// The text the player's F3 overlay and the editor's "Renderer:" line show for a
/// GPU backend that drew the frame. Software has no fixed text (the host says why).
inline const char* GpuBackendLabel(RenderBackend b) {
    switch (b) {
        case RenderBackend::OpenGL:     return "OpenGL (GPU)";
        case RenderBackend::Direct3D11: return "Direct3D 11 (GPU)";
        case RenderBackend::Direct3D12: return "Direct3D 12 (GPU)";
        default:                        return "SOFTWARE (CPU)";
    }
}

/// One plain line per backend for the `--gpu-probe` report and logs.
inline std::string GpuCapabilitiesText(RenderBackend b) {
    const GpuCapabilities& c = GpuBackendCapabilities(b);
    auto yn = [](bool v) { return v ? "yes" : "no"; };
    return std::string(GpuBackendId(b)) + ": lights " + std::to_string(c.maxLights) + ", MSAA " + std::to_string(c.maxMsaaSamples) +
           "x, sun shadows " + yn(c.shadows) + ", local shadows " + yn(c.localShadows) + ", GPU skinning " + yn(c.gpuSkinning) +
           ", post chain " + yn(c.postChain) + ", direct present " + yn(c.directPresent) + ", compute " + yn(c.compute);
}

inline bool ParseGpuBackendId(const std::string& s, RenderBackend& out) {
    for (int i = 0; i < kRenderBackendCount; ++i)
        if (s == GpuBackendId(static_cast<RenderBackend>(i))) { out = static_cast<RenderBackend>(i); return true; }
    return false;
}

/// Failure history of the GPU path as a whole (not per backend: that is how both
/// hosts count today). `limit` failed frames in a row trip it. With cooldownMs == 0
/// it stays off for good (player); otherwise it rests for cooldownMs and counts
/// again from zero (editor: 5000). Times are millisecond ticks that may wrap.
struct GpuFailureTracker {
    int  fails = 0;
    bool off = false;          // tripped with no cooldown: never retry
    bool resting = false;      // tripped with a cooldown: wait until retryAt
    std::uint32_t retryAt = 0;

    bool MayTry(std::uint32_t nowMs) const {
        if (off) return false;
        if (!resting) return true;
        return static_cast<std::int32_t>(nowMs - retryAt) >= 0;
    }
    /// Returns true when this call tripped the tracker (the host may log it).
    bool Record(bool frameWorked, std::uint32_t nowMs, int limit = 3, std::uint32_t cooldownMs = 0) {
        if (frameWorked) { fails = 0; resting = false; return false; }
        if (++fails < limit) return false;
        if (cooldownMs == 0) { off = true; }
        else { fails = 0; resting = true; retryAt = nowMs + cooldownMs; }
        return true;
    }
};

struct GpuChooserInput {
    bool available[kRenderBackendCount] = {true, false, false, false};  // compiled in AND started OK
    bool gpuEnabled = true;       // false = skip every GPU backend (`gpu=0`, editor GPU view off)
    bool allowD3D12 = false;      // player opt-in; the editor leaves it false
    bool tryAllowed = true;       // GpuFailureTracker::MayTry(now)
    bool disabled[kRenderBackendCount] = {};  // a backend a host has demoted for this run
    GpuRequirements need;         // default asks for nothing
};

struct GpuBackendOrder {
    RenderBackend list[kRenderBackendCount] = {};
    int count = 0;     // always >= 1: the last entry is Software
    bool Has(RenderBackend b) const {
        for (int i = 0; i < count; ++i) if (list[i] == b) return true;
        return false;
    }
    int GpuCount() const { return count > 0 ? count - 1 : 0; }   // entries before Software
};

/// The order to try, best first, ending in Software. Same picks as the old chains:
/// D3D12 (when allowed), D3D11, OpenGL. Preference order is fixed in this slice.
inline GpuBackendOrder ChooseGpuBackends(const GpuChooserInput& in) {
    GpuBackendOrder o;
    if (in.gpuEnabled && in.tryAllowed) {
        static const RenderBackend kPrefer[] = {RenderBackend::Direct3D12, RenderBackend::Direct3D11, RenderBackend::OpenGL};
        for (RenderBackend b : kPrefer) {
            const int i = static_cast<int>(b);
            if (b == RenderBackend::Direct3D12 && !in.allowD3D12) continue;
            if (!in.available[i] || in.disabled[i]) continue;
            if (!GpuMeetsRequirements(GpuBackendCapabilities(b), in.need)) continue;
            o.list[o.count++] = b;
        }
    }
    o.list[o.count++] = RenderBackend::Software;
    return o;
}

}  // namespace okay
