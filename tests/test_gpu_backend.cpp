// Which GPU renderer a built game tries, and the Direct3D 12 preview label
// (Render/GpuBackend.hpp): default off, the plan per platform, the pick when some
// backends fail to start, and the list of effects D3D12 does not draw.
#include "test_framework.hpp"
#include "okay/Render/GpuBackend.hpp"
#include <fstream>
#include <sstream>
#include <string>

using namespace okay;

namespace {
std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
bool Has(const std::string& s, const char* n) { return s.find(n) != std::string::npos; }
bool Contains(const std::vector<std::string>& v, const char* n) { for (const auto& x : v) if (x == n) return true; return false; }
}

int main() {
    RUN_SUITE("gpu_backend");

    // The label says what it is, and Direct3D 12 is off by default.
    CHECK(std::string(kD3D12PreviewLabel) == "Direct3D 12 (preview: no post-processing)");
    CHECK(std::string(GpuBackendLabel(GpuBackend::Direct3D12)) == kD3D12PreviewLabel);
    CHECK(std::string(GpuBackendLabel(GpuBackend::Direct3D11)) == "Direct3D 11");
    CHECK(std::string(GpuBackendLabel(GpuBackend::OpenGL)) == "OpenGL");
    CHECK(!kPreferD3D12Default);
    CHECK(Has(kD3D12PreviewTip, "Direct3D 11 is the default"));

    // Plan: default config on Windows tries D3D11 then OpenGL, never D3D12.
    {
        GpuBackendPlan p = PlanGpuBackends(true, kPreferD3D12Default, true);
        CHECK(!p.tryD3D12 && p.tryD3D11 && p.tryOpenGL);
        p = PlanGpuBackends(true, true, true);                 // asked for the preview on Windows
        CHECK(p.tryD3D12 && p.tryD3D11 && p.tryOpenGL);
        p = PlanGpuBackends(true, true, false);                // D3D12 does not exist off Windows
        CHECK(!p.tryD3D12 && !p.tryD3D11 && p.tryOpenGL);
        p = PlanGpuBackends(true, false, false);
        CHECK(!p.tryD3D12 && !p.tryD3D11 && p.tryOpenGL);
        p = PlanGpuBackends(false, true, true);                // GPU renderer off: software only
        CHECK(!p.tryD3D12 && !p.tryD3D11 && !p.tryOpenGL);
    }

    // Pick: the first backend that is both planned and started.
    {
        GpuBackend b = GpuBackend::OpenGL;
        const GpuBackendPlan all = PlanGpuBackends(true, true, true);
        CHECK(PickGpuBackend(all, true, true, true, b) && b == GpuBackend::Direct3D12);
        CHECK(PickGpuBackend(all, false, true, true, b) && b == GpuBackend::Direct3D11);   // D3D12 failed to start: falls back
        CHECK(PickGpuBackend(all, false, false, true, b) && b == GpuBackend::OpenGL);
        CHECK(!PickGpuBackend(all, false, false, false, b));                                  // none: software
        const GpuBackendPlan def = PlanGpuBackends(true, false, true);
        CHECK(PickGpuBackend(def, true, true, true, b) && b == GpuBackend::Direct3D11);      // a started D3D12 is ignored when not asked for
        const GpuBackendPlan none = PlanGpuBackends(false, true, true);
        CHECK(!PickGpuBackend(none, true, true, true, b));
    }

    // The effects the preview does not draw.
    {
        BloomEnabled() = false; SSAOEnabled() = false;
        Scene::RenderSettings rs;
        CHECK(D3D12UnsupportedEffects(rs).empty());            // a default scene loses nothing the user asked for
        rs.post.bloom = true; rs.post.ssao = true; rs.post.fxaa = true; rs.post.dof = true; rs.post.grading = true;
        rs.post.exposure = 1.5f;
        auto v = D3D12UnsupportedEffects(rs);
        CHECK(Contains(v, "bloom") && Contains(v, "ambient occlusion") && Contains(v, "FXAA") && Contains(v, "depth of field"));
        CHECK(Contains(v, "colour grading") && Contains(v, "exposure"));
        Scene::RenderSettings g;
        SSAOEnabled() = true; BloomEnabled() = true;           // the global Lighting switches count too
        v = D3D12UnsupportedEffects(g);
        CHECK(v.size() == 2 && Contains(v, "bloom") && Contains(v, "ambient occlusion"));
        SSAOEnabled() = false; BloomEnabled() = false;
        CHECK(JoinEffectNames({}) == "");
        CHECK(JoinEffectNames({"bloom"}) == "bloom");
        CHECK(JoinEffectNames({"bloom", "FXAA"}) == "bloom and FXAA");
        CHECK(JoinEffectNames({"bloom", "ambient occlusion", "FXAA"}) == "bloom, ambient occlusion and FXAA");
    }

    // The Build Settings box and the player use these names (a source check: the UI cannot be driven here).
    {
        const std::string ed = ReadFile(std::string(OKAY_TEST_SOURCE_DIR) + "/editor/main.cpp");
        const std::string pl = ReadFile(std::string(OKAY_TEST_SOURCE_DIR) + "/player/main.cpp");
        CHECK(ed.size() > 1000 && pl.size() > 1000);
        CHECK(Has(ed, "ImGui::Checkbox(okay::kD3D12PreviewLabel, &g_build.preferD3D12)"));
        CHECK(Has(ed, "okay::D3D12UnsupportedEffects(ed.scene().renderSettings)"));
        CHECK(Has(ed, "bool preferD3D12 = okay::kPreferD3D12Default"));
        CHECK(!Has(ed, "Prefer Direct3D 12 (Windows)"));
        CHECK(Has(pl, "okay::PlanGpuBackends(cfg.gpu, cfg.d3d12, kHostIsWindows)"));
        CHECK(Has(pl, "if (gpuPlan.tryD3D12)"));
        CHECK(Has(pl, "bool d3d12 = okay::kPreferD3D12Default"));
    }
    TEST_MAIN_RESULT();
}
