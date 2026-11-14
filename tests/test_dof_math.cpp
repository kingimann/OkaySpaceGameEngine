#include "test_framework.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Render/DofMath.hpp"
#include "okay/Render/SsaoMath.hpp"
#include <fstream>
#include <sstream>
#include <string>

using namespace okay;

// Depth of field maths (DofMath.hpp), on the CPU, plus a text check that the
// OpenGL shader, the Direct3D 11 shader and the header agree.
namespace {
std::string Slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
} // namespace

int main() {
    RUN_SUITE("dof_math");
    using namespace okay::dof;
    const float F = okay::ssao::kFar;

    // Distance: depth * far, sky = 4 x far.
    CHECK_NEAR(Distance(0.2f, F), 30.0f, 1e-4f);
    CHECK_NEAR(Distance(0.995f, F), F * 4.0f, 1e-3f);
    CHECK_NEAR(Distance(1.0f, F), F * 4.0f, 1e-3f);
    CHECK(Distance(0.994f, F) < F);

    // Coc: zero inside the focus zone (focus +- range/2), ramps to 1 over one range.
    CHECK_NEAR(Coc(10.0f, 10.0f, 4.0f), 0.0f, 1e-6f);
    CHECK_NEAR(Coc(11.9f, 10.0f, 4.0f), 0.0f, 1e-6f);     // inside the zone
    CHECK_NEAR(Coc(12.0f, 10.0f, 4.0f), 0.0f, 1e-6f);     // edge of the zone
    CHECK_NEAR(Coc(14.0f, 10.0f, 4.0f), 0.5f, 1e-5f);     // half way up the ramp
    CHECK_NEAR(Coc(16.0f, 10.0f, 4.0f), 1.0f, 1e-5f);     // one range past the edge
    CHECK_NEAR(Coc(500.0f, 10.0f, 4.0f), 1.0f, 1e-6f);    // clamped
    CHECK_NEAR(Coc(6.0f, 10.0f, 4.0f), Coc(14.0f, 10.0f, 4.0f), 1e-6f);   // symmetric around focus
    CHECK(Coc(13.0f, 10.0f, 4.0f) < Coc(15.0f, 10.0f, 4.0f));              // monotonic
    CHECK(Coc(15.0f, 10.0f, 20.0f) < Coc(15.0f, 10.0f, 2.0f));             // wider range, softer
    // Sky is far past any sane focus, so it blurs fully.
    CHECK_NEAR(Coc(Distance(1.0f, F), 10.0f, 5.0f), 1.0f, 1e-6f);
    // A tiny range does not divide by zero.
    float c = Coc(5.0f, 10.0f, 0.0f);
    CHECK(c >= 0.0f && c <= 1.0f);

    // Mix: coc 0 keeps the sharp frame, 1 gives the blur.
    CHECK_NEAR(Mix(0.2f, 0.8f, 0.0f), 0.2f, 1e-6f);
    CHECK_NEAR(Mix(0.2f, 0.8f, 1.0f), 0.8f, 1e-6f);
    CHECK_NEAR(Mix(0.2f, 0.8f, 0.5f), 0.5f, 1e-6f);

    // Defaults: the Post-processing settings stay off, so no existing scene changes.
    Scene::RenderSettings rs;
    CHECK(!rs.post.dof);

    // Constants agree with the SSAO depth image they read.
    CHECK_NEAR(kBackground, okay::ssao::kBackground, 1e-6f);

    // Shader text: header HLSL, OpenGL shader, Direct3D 11 renderer.
    const std::string hlsl = OKAY_DOF_HLSL;
    CHECK(Has(hlsl, "PSDof("));
    CHECK(Has(hlsl, "dn >= " + std::string(OKAY_DOF_STR(OKAY_DOF_BACKGROUND))));
    CHECK(Has(hlsl, "uP7.z * " + std::string(OKAY_DOF_STR(OKAY_DOF_SKY_MUL))));
    CHECK(Has(hlsl, "uP7.y * " + std::string(OKAY_DOF_STR(OKAY_DOF_HALF))));
    CHECK(Has(hlsl, "max(uP7.y, " + std::string(OKAY_DOF_STR(OKAY_DOF_MIN_RANGE)) + ")"));
    CHECK(Has(hlsl, "abs(d - uP7.x)"));
    CHECK(Has(hlsl, "0.0, 1.0)"));

    std::string here = __FILE__;
    std::string root = here.substr(0, here.find_last_of("/\\") + 1) + "../engine/src/Render/";
    const std::string gl = Slurp(root + "GLRenderer.cpp");
    const std::string dx = Slurp(root + "D3D11Renderer.cpp");
    CHECK(!gl.empty() && !dx.empty());
    if (!gl.empty()) {
        CHECK(Has(gl, "dn >= 0.995 ? uFar * 4.0 : dn * uFar"));
        CHECK(Has(gl, "abs(d - uFocus) - uRange * 0.5) / max(uRange, 1e-3), 0.0, 1.0"));
        CHECK(Has(gl, "mix(texture2D(uSrc, vUV), texture2D(uBlur, vUV), coc)"));
        CHECK(Has(gl, "g.Uniform1f(m_udfFar, kSsaoFar)"));
    }
    if (!dx.empty()) {
        CHECK(Has(dx, "OKAY_DOF_HLSL"));
        CHECK(Has(dx, "psDof"));
        CHECK(Has(dx, "pc.p7[2] = ssao::kFar"));
        CHECK(Has(dx, "pc.p7[0] = ps.dofFocus; pc.p7[1] = ps.dofRange"));
        CHECK(Has(dx, "rsTop.post.dof"));     // runs only when the scene's switch is on
    }
    TEST_MAIN_RESULT();
}
