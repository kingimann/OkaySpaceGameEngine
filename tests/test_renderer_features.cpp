#include "test_framework.hpp"
#include "okay/Render/RendererFeatures.hpp"
#include <cstring>

using namespace okay;

// The support matrix below is the reading of the four renderers (see the
// comment block in RendererFeatures.hpp). A change to the table has to update
// this list, so a silent flip of "this switch does nothing" fails here.

static FeatureSupport Expect(RenderFeature feature, RenderBackend backend) {
    using F = RenderFeature;
    using B = RenderBackend;
    using S = FeatureSupport;
    struct Cell { F f; B b; S s; };
    static const Cell k[] = {
        {F::FilmicLighting, B::Software, S::No}, {F::FilmicLighting, B::OpenGL, S::Yes},
        {F::FilmicLighting, B::Direct3D11, S::Yes}, {F::FilmicLighting, B::Direct3D12, S::No},

        {F::Exposure, B::Software, S::No}, {F::Exposure, B::OpenGL, S::Yes},
        {F::Exposure, B::Direct3D11, S::Yes}, {F::Exposure, B::Direct3D12, S::No},

        {F::TonemapCurve, B::Software, S::No}, {F::TonemapCurve, B::OpenGL, S::Yes},
        {F::TonemapCurve, B::Direct3D11, S::Yes}, {F::TonemapCurve, B::Direct3D12, S::No},

        {F::SceneBloom, B::Software, S::No}, {F::SceneBloom, B::OpenGL, S::Yes},
        {F::SceneBloom, B::Direct3D11, S::Yes}, {F::SceneBloom, B::Direct3D12, S::No},

        {F::ColourGrading, B::Software, S::No}, {F::ColourGrading, B::OpenGL, S::Yes},
        {F::ColourGrading, B::Direct3D11, S::Yes}, {F::ColourGrading, B::Direct3D12, S::No},

        {F::VignetteAmount, B::Software, S::Yes}, {F::VignetteAmount, B::OpenGL, S::Yes},
        {F::VignetteAmount, B::Direct3D11, S::Yes}, {F::VignetteAmount, B::Direct3D12, S::Yes},

        {F::VignetteSmoothness, B::Software, S::No}, {F::VignetteSmoothness, B::OpenGL, S::Yes},
        {F::VignetteSmoothness, B::Direct3D11, S::Yes}, {F::VignetteSmoothness, B::Direct3D12, S::No},

        {F::SceneFxaa, B::Software, S::No}, {F::SceneFxaa, B::OpenGL, S::Yes},
        {F::SceneFxaa, B::Direct3D11, S::Yes}, {F::SceneFxaa, B::Direct3D12, S::No},

        {F::SceneSsao, B::Software, S::No}, {F::SceneSsao, B::OpenGL, S::Yes},
        {F::SceneSsao, B::Direct3D11, S::Yes}, {F::SceneSsao, B::Direct3D12, S::No},

        {F::DepthOfField, B::Software, S::No}, {F::DepthOfField, B::OpenGL, S::Yes},
        {F::DepthOfField, B::Direct3D11, S::No}, {F::DepthOfField, B::Direct3D12, S::No},

        {F::ProceduralSkyDetail, B::Software, S::No}, {F::ProceduralSkyDetail, B::OpenGL, S::Yes},
        {F::ProceduralSkyDetail, B::Direct3D11, S::Yes}, {F::ProceduralSkyDetail, B::Direct3D12, S::No},

        {F::Fog, B::Software, S::Yes}, {F::Fog, B::OpenGL, S::Yes},
        {F::Fog, B::Direct3D11, S::Yes}, {F::Fog, B::Direct3D12, S::Yes},

        {F::DirectionalShadows, B::Software, S::Yes}, {F::DirectionalShadows, B::OpenGL, S::Yes},
        {F::DirectionalShadows, B::Direct3D11, S::Yes}, {F::DirectionalShadows, B::Direct3D12, S::Yes},

        {F::SunShadowCascades, B::Software, S::Yes}, {F::SunShadowCascades, B::OpenGL, S::Yes},
        {F::SunShadowCascades, B::Direct3D11, S::Yes}, {F::SunShadowCascades, B::Direct3D12, S::No},

        {F::SpotPointShadows, B::Software, S::Yes}, {F::SpotPointShadows, B::OpenGL, S::Yes},
        {F::SpotPointShadows, B::Direct3D11, S::Yes}, {F::SpotPointShadows, B::Direct3D12, S::No},

        {F::BuildBloom, B::Software, S::Yes}, {F::BuildBloom, B::OpenGL, S::Yes},
        {F::BuildBloom, B::Direct3D11, S::WhenPostChainRuns}, {F::BuildBloom, B::Direct3D12, S::No},

        {F::BuildSsao, B::Software, S::Yes}, {F::BuildSsao, B::OpenGL, S::Yes},
        {F::BuildSsao, B::Direct3D11, S::WhenPostChainRuns}, {F::BuildSsao, B::Direct3D12, S::No},

        {F::BuildFxaa, B::Software, S::Yes}, {F::BuildFxaa, B::OpenGL, S::No},
        {F::BuildFxaa, B::Direct3D11, S::No}, {F::BuildFxaa, B::Direct3D12, S::No},

        {F::PerPixelLighting, B::Software, S::Yes}, {F::PerPixelLighting, B::OpenGL, S::No},
        {F::PerPixelLighting, B::Direct3D11, S::No}, {F::PerPixelLighting, B::Direct3D12, S::No},

        {F::SoftwareToneMap, B::Software, S::Yes}, {F::SoftwareToneMap, B::OpenGL, S::No},
        {F::SoftwareToneMap, B::Direct3D11, S::No}, {F::SoftwareToneMap, B::Direct3D12, S::No},

        {F::SoftwareColorGrade, B::Software, S::Yes}, {F::SoftwareColorGrade, B::OpenGL, S::No},
        {F::SoftwareColorGrade, B::Direct3D11, S::No}, {F::SoftwareColorGrade, B::Direct3D12, S::No},
    };
    for (const Cell& c : k)
        if (c.f == feature && c.b == backend) return c.s;
    return FeatureSupport::No;
}

int main() {
    RUN_SUITE("renderer_features");

    int cells = 0;
    for (int fi = 0; fi < static_cast<int>(RenderFeature::Count); ++fi) {
        const auto feature = static_cast<RenderFeature>(fi);
        bool anyCaveat = false;
        for (int bi = 0; bi < kRenderBackendCount; ++bi) {
            const auto backend = static_cast<RenderBackend>(bi);
            const FeatureSupport got = RendererSupports(feature, backend);
            const FeatureSupport want = Expect(feature, backend);
            CHECK(got == want);
            if (want != FeatureSupport::Yes) anyCaveat = true;
            ++cells;
        }
        const char* note = FeatureGapNote(feature);
        CHECK(note != nullptr);
        CHECK((note[0] == 0) != anyCaveat);
    }
    CHECK(cells == static_cast<int>(RenderFeature::Count) * kRenderBackendCount);

    // The Light Inspector sentence, including the area-light rider.
    CHECK(std::strcmp(FeatureGapNote(RenderFeature::SpotPointShadows),
                      "Spot and point light shadows are not drawn on Direct3D 12. "
                      "Area lights use the same shadow path, so they are not drawn there either. "
                      "The software renderer draws them, like sun shadows, only with Per-pixel lighting on.") == 0);

    CHECK(std::strcmp(FeatureGapNote(RenderFeature::SceneSsao),
                      "Ambient occlusion in Post-processing is not drawn on Direct3D 12 or the software renderer. "
                      "The Ambient occlusion switch under Rendering, and in Build Settings, is a separate switch.") == 0);

    CHECK(std::strcmp(FeatureGapNote(RenderFeature::DepthOfField),
                      "Depth of field is not drawn on Direct3D 12 or the software renderer.") == 0);

    CHECK(std::strcmp(FeatureGapNote(RenderFeature::BuildBloom),
                      "Bloom in Build Settings and Rendering is not drawn on Direct3D 12. "
                      "Direct3D 11 draws it only while the scene post-processing chain is running.") == 0);

    CHECK(std::strcmp(FeatureGapNote(RenderFeature::BuildSsao),
                      "Ambient occlusion in Build Settings and Rendering is not drawn on Direct3D 12. "
                      "Direct3D 11 draws it only while the scene post-processing chain is running.") == 0);

    CHECK(std::strcmp(FeatureGapNote(RenderFeature::PerPixelLighting),
                      "Per-pixel lighting changes only the software renderer. "
                      "OpenGL, Direct3D 11 and Direct3D 12 always light per pixel.") == 0);

    // Drawn everywhere: the note stays quiet.
    CHECK(FeatureGapNote(RenderFeature::Fog)[0] == 0);
    CHECK(FeatureGapNote(RenderFeature::VignetteAmount)[0] == 0);
    CHECK(FeatureGapNote(RenderFeature::DirectionalShadows)[0] == 0);

    // Same pointer every call (the editor paints this every frame).
    CHECK(FeatureGapNote(RenderFeature::SpotPointShadows) == FeatureGapNote(RenderFeature::SpotPointShadows));

    TEST_MAIN_RESULT();
}
