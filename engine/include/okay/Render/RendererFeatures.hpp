#pragma once
// ---------------------------------------------------------------------------
// Which lighting, post, fog and sky switches each renderer actually draws.
//
// The table is read off the renderers. It does not change what they draw, and
// it does not turn any switch on. Backends, in RenderBackend order:
//
//   Software   engine/include/okay/Render/SoftwareRenderer.hpp
//              FogFactor for every fog mode (RenderMeshes). Sun cascades via
//              ShadowDistance / ShadowCascades (RenderShadowMap). No local
//              light shadow map. Scene post settings: post.exposure, the
//              tonemap curve and rs.tonemap, post.grading, the vignette (with
//              smoothness; PostVignetteHandled() is set true so the host skips
//              its overlay), post.bloom and post.fxaa are read (PlanScenePost /
//              ApplyScenePost). post.ssao and post.dof are not. Procedural sky colours come
//              from EffectiveSkyColors; the disc, halo, clouds and stars are not
//              drawn. ApplySSAO reads the process-wide switch only; ApplyBloom, ApplyFXAA
//              and ApplyColorGrade also still read theirs. PerPixelLighting is
//              read only here, and ShadowsUsePerPixel (default on) lights per
//              pixel while a shadow map exists, so shadows show by default.
//
//   OpenGL     engine/src/Render/GLRenderer.cpp
//              Mesh shader uTonemap from rs.tonemap. Post chain (PostActive):
//              exposure, tonemap curve, bloom, grading, vignette smoothness,
//              FXAA (post.fxaa), SSAO (post.ssao or SSAOEnabled), depth of
//              field. BloomEnabled and SSAOEnabled also run when the chain is
//              off. FXAAEnabled is not read. PlanShadows draws sun cascades and
//              spot / point / area tiles (localShadow). Procedural sky pass
//              draws the disc, halo, haze, clouds and stars.
//
//   Direct3D11 engine/src/Render/D3D11Renderer.cpp
//              Same mesh fog modes, filmic flag, sun cascades and localShadow
//              tiles as OpenGL. Post chain draws exposure, the tonemap curve,
//              bloom (post.bloom or BloomEnabled), grading, vignette smoothness
//              and post.fxaa. No depth of field. BloomEnabled does
//              nothing unless the chain is already running (it does not start
//              the chain). SSAO (post.ssao, or SSAOEnabled while the chain runs)
//              is an extra pass inside the chain (SsaoMath.hpp). UNVERIFIED on a
//              real GPU: compile-checked with MinGW only. FXAAEnabled is not
//              read. Procedural
//              sky shader draws the disc, halo, haze, clouds and stars.
//
//   Direct3D12 engine/src/Render/D3D12Renderer.cpp
//              Fog modes and EffectiveFogColor. One directional shadow map
//              (ComputeDirectionalShadowVP, which uses ShadowDistance and not
//              ShadowCascades). Non-directional lights are forced lit
//              (lsh = 1). Preview backend: no post chain: exposure, tonemap, bloom, grading,
//              FXAA, SSAO and depth of field are absent. The host paints the
//              vignette (PostVignetteHandled is never set). Procedural sky
//              detail is absent; EffectiveSkyColors still feeds the gradient.
//
// The vignette amount is drawn on every backend: the post chain when it runs
// (OpenGL and Direct3D 11), otherwise the host overlay (editor and player),
// which ignores vignette smoothness.
// ---------------------------------------------------------------------------
#include <string>

namespace okay {

enum class RenderBackend : int {
    Software = 0,
    OpenGL = 1,
    Direct3D11 = 2,
    Direct3D12 = 3,
};
inline constexpr int kRenderBackendCount = 4;

enum class RenderFeature : int {
    FilmicLighting = 0,    // scene "Filmic Rendering" (rs.tonemap)
    Exposure,              // scene post.exposure
    TonemapCurve,          // scene post.tonemap (None / ACES / Filmic)
    SceneBloom,            // scene post.bloom
    ColourGrading,         // scene post.grading
    VignetteAmount,        // scene rs.vignette
    VignetteSmoothness,    // scene post.vignetteSmooth
    SceneFxaa,             // scene post.fxaa
    SceneSsao,             // scene post.ssao
    DepthOfField,          // scene post.dof
    ProceduralSkyDetail,   // sun disc, halo, haze, clouds, stars
    Fog,                   // linear, exponential, height, match sky
    DirectionalShadows,    // a directional light's Cast Shadows
    SunShadowCascades,     // cascade count (shadow distance still applies on D3D12)
    SpotPointShadows,      // Cast Shadows on a spot, point or area light
    BuildBloom,            // BloomEnabled (Build Settings and Rendering)
    BuildSsao,             // SSAOEnabled
    BuildFxaa,             // FXAAEnabled
    PerPixelLighting,      // PerPixelLighting()
    SoftwareToneMap,       // ToneMapEnabled and its Exposure slider
    SoftwareColorGrade,    // ColorGradeEnabled in Rendering
    Count
};

/// Yes: the switch changes that backend's picture.
/// No: the switch is saved and ignored.
/// WhenPostChainRuns: drawn only while the scene post chain is already on.
///   It does not turn the chain on by itself.
enum class FeatureSupport : int {
    No = 0,
    Yes = 1,
    WhenPostChainRuns = 2,
};

struct RenderFeatureRow {
    const char* subject;   // plain-English name of the switch
    bool plural;           // "are" / "is"
    const char* extra;     // second sentence, or nullptr
    const char* instead;   // whole note, when "not drawn" would mislead; or nullptr
    FeatureSupport support[kRenderBackendCount]; // Software, OpenGL, Direct3D11, Direct3D12
};

inline const RenderFeatureRow& RenderFeatureRowAt(RenderFeature feature) {
    using S = FeatureSupport;
    // Indexed by RenderFeature. Keep this list in that order.
    static const RenderFeatureRow kRows[] = {
        {"Linear lighting and the tonemap roll-off", false, nullptr, nullptr, {S::No, S::Yes, S::Yes, S::No}},
        {"Exposure", false, nullptr, nullptr, {S::Yes, S::Yes, S::Yes, S::No}},
        {"The tonemap curve", false, nullptr, nullptr, {S::Yes, S::Yes, S::Yes, S::No}},
        {"Bloom in Post-processing", false, nullptr, nullptr, {S::Yes, S::Yes, S::Yes, S::No}},
        {"Colour grading", false, nullptr, nullptr, {S::Yes, S::Yes, S::Yes, S::No}},
        {"The vignette", false, nullptr, nullptr, {S::Yes, S::Yes, S::Yes, S::Yes}},
        {"Vignette smoothness", false,
         "The vignette amount itself is drawn on every renderer.", nullptr,
         {S::Yes, S::Yes, S::Yes, S::No}},
        {"Anti-aliasing in Post-processing", false, nullptr, nullptr, {S::Yes, S::Yes, S::Yes, S::No}},
        {"Ambient occlusion in Post-processing", false,
         "The Ambient occlusion switch under Rendering, and in Build Settings, is a separate switch.",
         nullptr, {S::No, S::Yes, S::Yes, S::No}},
        {"Depth of field", false, nullptr, nullptr, {S::No, S::Yes, S::No, S::No}},
        {"The procedural sun disc, halo, clouds and stars", true,
         "Direct3D 12 and the software renderer show the sun's colours as a plain gradient.",
         nullptr, {S::No, S::Yes, S::Yes, S::No}},
        {"Fog", false, nullptr, nullptr, {S::Yes, S::Yes, S::Yes, S::Yes}},
        {"Sun shadows", true, nullptr, nullptr, {S::Yes, S::Yes, S::Yes, S::Yes}},
        {"Extra sun shadow cascades", true,
         "Direct3D 12 draws one sun shadow map. Shadow distance still applies there.",
         nullptr, {S::Yes, S::Yes, S::Yes, S::No}},
        {"Spot and point light shadows", true,
         "Area lights use the same shadow path, so they are not drawn there either. "
         "The software renderer draws them, like sun shadows, by default: it lights per pixel while a shadow is on screen.",
         nullptr, {S::Yes, S::Yes, S::Yes, S::No}},
        {"Bloom in Build Settings and Rendering", false, nullptr, nullptr,
         {S::Yes, S::Yes, S::WhenPostChainRuns, S::No}},
        {"Ambient occlusion in Build Settings and Rendering", false, nullptr, nullptr,
         {S::Yes, S::Yes, S::WhenPostChainRuns, S::No}},
        {"Anti-aliasing in Build Settings and Rendering", false, nullptr, nullptr,
         {S::Yes, S::No, S::No, S::No}},
        {"Per-pixel lighting", false, nullptr,
         "Per-pixel lighting changes only the software renderer. OpenGL, Direct3D 11 and Direct3D 12 always light per pixel.",
         {S::Yes, S::No, S::No, S::No}},
        {"Tone mapping in Rendering", false,
         "OpenGL and Direct3D 11 use Filmic Rendering and the tonemap curve in Post-processing instead.",
         nullptr, {S::Yes, S::No, S::No, S::No}},
        {"Color grading in Rendering", false,
         "OpenGL and Direct3D 11 use Colour grading in Post-processing instead.",
         nullptr, {S::Yes, S::No, S::No, S::No}},
    };
    static_assert(sizeof(kRows) / sizeof(kRows[0]) == static_cast<int>(RenderFeature::Count),
                  "one row per render feature");
    const int i = static_cast<int>(feature);
    return kRows[(i < 0 || i >= static_cast<int>(RenderFeature::Count)) ? 0 : i];
}

inline FeatureSupport RendererSupports(RenderFeature feature, RenderBackend backend) {
    const int b = static_cast<int>(backend);
    if (b < 0 || b >= kRenderBackendCount) return FeatureSupport::No;
    return RenderFeatureRowAt(feature).support[b];
}

inline const char* RenderBackendName(RenderBackend backend) {
    switch (backend) {
    case RenderBackend::Software:   return "the software renderer";
    case RenderBackend::OpenGL:     return "OpenGL";
    case RenderBackend::Direct3D11: return "Direct3D 11";
    case RenderBackend::Direct3D12: return "Direct3D 12";
    }
    return "";
}

/// Order used when a note lists the backends that ignore a switch.
inline RenderBackend RenderBackendMentionOrder(int index) {
    static const RenderBackend kOrder[] = {
        RenderBackend::Direct3D11, RenderBackend::Direct3D12,
        RenderBackend::OpenGL, RenderBackend::Software};
    return kOrder[index];
}

/// Plain-English note for a switch some backends ignore. Empty when every
/// backend draws it with no caveat. Stable for the life of the process.
inline const char* FeatureGapNote(RenderFeature feature) {
    static std::string notes[static_cast<int>(RenderFeature::Count)];
    static bool ready = false;
    if (!ready) {
        auto join = [](const RenderBackend* b, int n) {
            std::string s;
            for (int i = 0; i < n; ++i) {
                if (i) s += (i + 1 == n) ? " or " : ", ";
                s += RenderBackendName(b[i]);
            }
            return s;
        };
        for (int fi = 0; fi < static_cast<int>(RenderFeature::Count); ++fi) {
            const auto featureId = static_cast<RenderFeature>(fi);
            const RenderFeatureRow& row = RenderFeatureRowAt(featureId);
            if (row.instead) { notes[fi] = row.instead; continue; }
            RenderBackend missing[kRenderBackendCount];
            RenderBackend chain[kRenderBackendCount];
            int nMissing = 0, nChain = 0;
            for (int i = 0; i < kRenderBackendCount; ++i) {
                const RenderBackend b = RenderBackendMentionOrder(i);
                const FeatureSupport s = RendererSupports(featureId, b);
                if (s == FeatureSupport::No) missing[nMissing++] = b;
                else if (s == FeatureSupport::WhenPostChainRuns) chain[nChain++] = b;
            }
            std::string note;
            if (nMissing > 0) {
                note = row.subject;
                note += row.plural ? " are not drawn on " : " is not drawn on ";
                note += join(missing, nMissing);
                note += ".";
            }
            if (nChain > 0) {
                if (!note.empty()) note += " ";
                note += join(chain, nChain);
                note += nChain == 1 ? " draws it only while the scene post-processing chain is running."
                                    : " draw it only while the scene post-processing chain is running.";
            }
            if (row.extra && row.extra[0]) {
                if (!note.empty()) note += " ";
                note += row.extra;
            }
            notes[fi] = std::move(note);
        }
        ready = true;
    }
    const int i = static_cast<int>(feature);
    if (i < 0 || i >= static_cast<int>(RenderFeature::Count)) return "";
    return notes[i].c_str();
}

} // namespace okay
