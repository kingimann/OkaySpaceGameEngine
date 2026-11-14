#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/D3DPostHLSL.hpp"
#include <fstream>
#include <sstream>
#include <string>

// The post-chain HLSL shared by Direct3D 11 (and, next, Direct3D 12): text checks only.
// This cannot compile HLSL (no D3DCompile on Linux) and it runs on no GPU: UNVERIFIED there.
namespace {
std::string Slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::ostringstream o; o << f.rdbuf(); return o.str(); }
bool Has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }
int Count(const std::string& h, char c) { int n = 0; for (char x : h) n += x == c; return n; }
}

int main() {
    RUN_SUITE("d3d_post_hlsl");
    const std::string hlsl = OKAY_POST_HLSL;
    CHECK(!hlsl.empty());
    // Every post entry point is there, once.
    for (const char* e : {"float4 PSDown(PSQuad", "float4 PSBlur(PSQuad", "float4 PSUber(PSQuad", "float4 PSFxaa(PSQuad"})
        CHECK(Has(hlsl, e) && hlsl.find(e) == hlsl.rfind(e));
    // Braces and parentheses balance (a dropped line would show here).
    CHECK(Count(hlsl, '{') == Count(hlsl, '}'));
    CHECK(Count(hlsl, '(') == Count(hlsl, ')'));
    // The pieces the host must provide are used, so the header comment stays true.
    for (const char* n : {"uTex", "uNormalTex", "uSpecTex", "uSampClamp", "uTexelDir", "uGainRadius", "uLiftAspect", "uGammaThresh",
                          "uP0", "uP1", "uP2", "aces("})
        CHECK(Has(hlsl, n));
    // Same maths as the OpenGL uber pass (kUberFrag).
    CHECK(Has(hlsl, "1.0 + 0.25 * uP1.z") && Has(hlsl, "0.8 * uP2.z * smoothstep"));
    CHECK(Has(hlsl, "hable(x * 2.0) / hable(float3(11.2, 11.2, 11.2))"));

    std::string here = __FILE__;
    std::string root = here.substr(0, here.find_last_of("/\\") + 1) + "../engine/src/Render/";
    const std::string d11 = Slurp(root + "D3D11Renderer.cpp");
    CHECK(!d11.empty());
    // Direct3D 11 takes the text from the header and no longer carries its own copy.
    CHECK(Has(d11, "OKAY_POST_HLSL") && Has(d11, "D3DPostHLSL.hpp"));
    CHECK(!Has(d11, "float4 PSUber(PSQuad") && !Has(d11, "float4 PSFxaa(PSQuad"));
    // Entry points that D3D11 compiles by name exist in the shared text.
    CHECK(Has(d11, "ps(\"PSDown\"") && Has(d11, "ps(\"PSUber\"") && Has(d11, "ps(\"PSFxaa\""));
    TEST_MAIN_RESULT();
}
