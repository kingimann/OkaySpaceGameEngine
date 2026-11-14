#include "test_framework.hpp"
#include "okay/Render/PostQualityMath.hpp"
#include "okay/Render/SsaoMath.hpp"
#include <fstream>
#include <sstream>
#include <string>

using namespace okay;

// Bloom soft knee and ambient occlusion power (PostQualityMath.hpp), on the CPU,
// plus a text check that the OpenGL and Direct3D 11 shaders paste the same code.
namespace {
std::string Slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
float OldGain(float l, float t) { return (l - t > 0.0f ? l - t : 0.0f) / (l > 1e-4f ? l : 1e-4f); }
} // namespace

int main() {
    RUN_SUITE("post_quality_math");
    using namespace okay::postq;

    // Softness 0 is exactly the old hard threshold, for any luma and threshold.
    for (float t : {0.0f, 0.3f, 0.7f, 1.0f, 2.5f})
        for (float l = 0.0f; l <= 4.0f; l += 0.05f)
            CHECK_NEAR(BloomGain(l, t, 0.0f), OldGain(l, t), 1e-6f);

    // With a soft knee: nothing below t - knee, continuous across the knee,
    // monotonic in luma, below the hard cut's glow inside the knee's upper half
    // never exceeds the pixel (gain <= 1), and some glow appears just under t.
    const float t = 1.0f, soft = 0.5f, knee = t * soft;
    CHECK_NEAR(BloomGain(t - knee - 0.01f, t, soft), 0.0f, 1e-6f);
    CHECK(BloomGain(t - 0.05f, t, soft) > 0.0f);                 // glows a little under the threshold
    CHECK_NEAR(OldGain(t - 0.05f, t), 0.0f, 1e-9f);              // the old cut gave none
    CHECK_NEAR(BloomGain(t + knee, t, soft), OldGain(t + knee, t), 1e-4f);   // meets the hard line at the top of the knee
    CHECK_NEAR(BloomGain(t + 2.0f, t, soft), OldGain(t + 2.0f, t), 1e-6f);   // far above: identical
    float prev = -1.0f;
    for (float l = 0.0f; l <= 3.0f; l += 0.01f) {
        float g = BloomGain(l, t, soft);
        CHECK(g >= prev - 1e-5f); CHECK(g >= 0.0f && g <= 1.0f + 1e-6f);
        prev = g;
    }
    // No jump: neighbouring lumas differ by a small step everywhere (the hard cut
    // is continuous in l - t too, so check the soft one is also gentler at the corner).
    float maxStepSoft = 0.0f;
    for (float l = 0.5f; l < 1.6f; l += 0.001f) {
        float d = BloomGain(l + 0.001f, t, soft) * (l + 0.001f) - BloomGain(l, t, soft) * l;   // glow amount, not gain
        if (d > maxStepSoft) maxStepSoft = d;
    }
    CHECK(maxStepSoft < 0.0021f);
    // More softness reaches further under the threshold.
    CHECK(BloomGain(0.8f, t, 0.8f) > BloomGain(0.8f, t, 0.3f));
    // Threshold 0 has no knee and stays defined.
    CHECK_NEAR(BloomGain(0.5f, 0.0f, 1.0f), 1.0f, 1e-5f);
    CHECK_NEAR(BloomGain(0.0f, 0.0f, 1.0f), 0.0f, 1e-6f);

    // AO power: 1 changes nothing, higher darkens, lower lightens, 1 and 0 are fixed points.
    for (float a : {0.0f, 0.2f, 0.5f, 0.9f, 1.0f}) CHECK_NEAR(AoCurve(a, 1.0f), a, 0.0f);
    CHECK_NEAR(AoCurve(0.5f, 2.0f), 0.25f, 1e-6f);
    CHECK(AoCurve(0.8f, 2.0f) < 0.8f && AoCurve(0.8f, 0.5f) > 0.8f);
    CHECK_NEAR(AoCurve(1.0f, 3.0f), 1.0f, 1e-6f);
    CHECK_NEAR(AoCurve(0.0f, 3.0f), 0.0f, 1e-6f);
    CHECK_NEAR(AoCurve(-0.1f, 2.0f), 0.0f, 1e-6f);               // never NaN
    // Power 1 reproduces ssao::Apply exactly.
    for (float a : {0.0f, 0.3f, 0.77f, 1.0f})
        for (float s : {0.0f, 0.4f, 0.8f, 1.0f, 2.0f})
            CHECK_NEAR(ApplyPowered(a, s, 1.0f), okay::ssao::Apply(a, s), 1e-7f);
    CHECK(ApplyPowered(0.7f, 1.0f, 2.0f) < ApplyPowered(0.7f, 1.0f, 1.0f));
    CHECK_NEAR(ApplyPowered(0.7f, 0.0f, 3.0f), 1.0f, 1e-7f);     // strength 0 is always off

    // Shader text: the same knee code in both renderers, wired to the settings.
    const std::string knee_src = OKAY_BLOOM_KNEE_SRC;
    CHECK(Has(knee_src, "float bloomK(float l, float t, float soft)"));
    CHECK(Has(knee_src, "clamp(l - t + knee, 0.0, 2.0 * knee)"));
    CHECK(Has(knee_src, "s * s / (4.0 * knee + 1e-5)"));
    CHECK(Has(knee_src, "max(max(s, l - t), 0.0) / max(l, 1e-4)"));
    std::string here = __FILE__;
    std::string root = here.substr(0, here.find_last_of("/\\") + 1) + "../engine/src/Render/";
    const std::string gl = Slurp(root + "GLRenderer.cpp");
    const std::string dx = Slurp(root + "D3D11Renderer.cpp") + Slurp(root + "../../include/okay/Render/D3DPostHLSL.hpp");   // the bright-pass lives in the shared post header
    CHECK(!gl.empty() && !dx.empty());
    if (!gl.empty()) {
        CHECK(Has(gl, "OKAY_BLOOM_KNEE_SRC"));
        CHECK(Has(gl, "bloomK(l, uThresh, uSoft)"));
        CHECK(Has(gl, "ps.bloom ? ps.bloomSoftness : 0.0f"));        // only the scene's switch softens
        CHECK(Has(gl, "if (uPower != 1.0) ao = pow(max(ao, vec3(0.0)), vec3(uPower))"));
        CHECK(Has(gl, "ps.ssao ? ps.ssaoPower : 1.0f"));
    }
    if (!dx.empty()) {
        CHECK(Has(dx, "OKAY_BLOOM_KNEE_SRC"));
        CHECK(Has(dx, "bloomK(l, uGammaThresh.w, uP7.w)"));
        CHECK(Has(dx, "pc.p7[3] = ps.bloom ? ps.bloomSoftness : 0.0f"));
        CHECK(Has(dx, "pc.p7[3] = rsTop.post.ssao ? rsTop.post.ssaoPower : 1.0f"));
    }
    CHECK(Has(OKAY_SSAO_HLSL, "if (uP7.w != 1.0) a = pow(max(a, 0.0), uP7.w);"));
    TEST_MAIN_RESULT();
}
