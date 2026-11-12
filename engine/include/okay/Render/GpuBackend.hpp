#pragma once
// Which GPU renderer a built game tries, and what the Direct3D 12 one cannot draw.
//
// Direct3D 12 is a PREVIEW backend: it renders the lit, textured, shadowed, fogged scene but
// has none of the post-processing the other GPU renderers share (exposure, tonemap, bloom,
// grading, FXAA, SSAO, depth of field; the vignette is painted by the host over the frame). It stays off unless Build Settings asks
// for it, and everything that mentions it uses these names so the label, the player's choice
// and the warnings cannot drift apart (tests/test_gpu_backend.cpp).
#include "okay/Render/Atmosphere.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // BloomEnabled(), SSAOEnabled()
#include <cmath>
#include <string>
#include <vector>

namespace okay {

enum class GpuBackend { Direct3D12, Direct3D11, OpenGL };

/// Build Settings starts with Direct3D 12 off; Direct3D 11 (and OpenGL) are the defaults.
inline constexpr bool kPreferD3D12Default = false;

/// The check box text, tooltip and player log line for Direct3D 12.
inline constexpr const char* kD3D12PreviewLabel = "Direct3D 12 (preview: no post-processing)";
inline constexpr const char* kD3D12PreviewTip =
    "A preview backend. It draws lit, textured, shadowed and fogged scenes, but NOT the post-processing\n"
    "the other GPU renderers share: exposure, tonemap, bloom, colour grading, FXAA, ambient occlusion\n"
    "and depth of field (the vignette is still painted over the frame). Characters are skinned on the CPU and spot / point lights cast no\n"
    "shadows. Leave it off unless you are testing it; Direct3D 11 is the default on Windows.\n"
    "If it cannot start, the game falls back to Direct3D 11, OpenGL, then software.";

/// Display name of a backend (Direct3D 12 carries its preview label).
inline const char* GpuBackendLabel(GpuBackend b) {
    switch (b) {
    case GpuBackend::Direct3D12: return kD3D12PreviewLabel;
    case GpuBackend::Direct3D11: return "Direct3D 11";
    default:                     return "OpenGL";
    }
}

/// Which backends the player tries to start, in this order: Direct3D 12 only when asked for
/// (Windows), then Direct3D 11 (Windows), then OpenGL. All false without a GPU renderer.
struct GpuBackendPlan { bool tryD3D12 = false, tryD3D11 = false, tryOpenGL = false; };
inline GpuBackendPlan PlanGpuBackends(bool gpuRenderer, bool preferD3D12, bool windows) {
    GpuBackendPlan p;
    p.tryD3D12 = gpuRenderer && windows && preferD3D12;
    p.tryD3D11 = gpuRenderer && windows;
    p.tryOpenGL = gpuRenderer;
    return p;
}

/// The backend a frame uses given which ones started: the first ready one in plan order.
/// Returns false when none is ready (the software renderer draws the frame).
inline bool PickGpuBackend(const GpuBackendPlan& plan, bool d3d12Ready, bool d3d11Ready, bool glReady, GpuBackend& out) {
    if (plan.tryD3D12 && d3d12Ready) { out = GpuBackend::Direct3D12; return true; }
    if (plan.tryD3D11 && d3d11Ready) { out = GpuBackend::Direct3D11; return true; }
    if (plan.tryOpenGL && glReady)   { out = GpuBackend::OpenGL;     return true; }
    return false;
}

/// The effects this scene asks for that the Direct3D 12 preview does not draw, by name
/// (empty = nothing is lost). Reads the scene's settings and the Lighting switches the
/// other renderers honour, so Build Settings and the player can say exactly what is missing.
inline std::vector<std::string> D3D12UnsupportedEffects(const Scene::RenderSettings& rs) {
    std::vector<std::string> v;
    const auto& p = rs.post;
    if (p.bloom || BloomEnabled()) v.push_back("bloom");
    if (p.ssao || SSAOEnabled()) v.push_back("ambient occlusion");
    if (p.dof) v.push_back("depth of field");
    if (p.grading) v.push_back("colour grading");
    if (p.fxaa) v.push_back("FXAA");
    if (std::fabs(p.exposure - 1.0f) > 1e-4f) v.push_back("exposure");
    if (rs.tonemap && p.tonemap != Scene::RenderSettings::PostSettings::TonemapACES) v.push_back("tonemap curve");
    return v;
}

/// "bloom, vignette and FXAA" style list for a message.
inline std::string JoinEffectNames(const std::vector<std::string>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s += (i + 1 == v.size()) ? " and " : ", ";
        s += v[i];
    }
    return s;
}

} // namespace okay
