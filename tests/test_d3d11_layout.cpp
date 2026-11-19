#include "test_framework.hpp"
#include "okay/Render/D3D11Hlsl.hpp"
#include "okay/Render/D3D11Layout.hpp"
#include <cctype>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// Offline checks of the Direct3D 11 mesh shader text (engine/include/okay/Render/D3D11Hlsl.hpp) as far as they can go without a GPU or
// the HLSL compiler: the constant buffers the shader declares have exactly the size of the C++ structs the renderer uploads (HLSL 16-byte
// packing rules), braces balance, every helper is declared before it is used, and no OpenGL-only names or shader-model-6 features crept in.
// This does NOT compile the HLSL: UNVERIFIED on a GPU.
using namespace okay;
namespace {
std::string Trim(std::string s) { while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(0, 1); while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back(); return s; }
// Size in floats of a cbuffer body under HLSL packing: a value never straddles a 16-byte row; an array element starts on a new row.
int PackedFloats(const std::string& body) {
    int off = 0;
    size_t pos = 0;
    while (pos < body.size()) {
        size_t semi = body.find(';', pos);
        if (semi == std::string::npos) break;
        std::string decl = Trim(body.substr(pos, semi - pos));
        pos = semi + 1;
        if (decl.empty()) continue;
        const size_t sp = decl.find(' ');
        const std::string type = decl.substr(0, sp);
        std::string name = Trim(decl.substr(sp));
        int count = 1;
        const size_t lb = name.find('[');
        if (lb != std::string::npos) count = std::atoi(name.c_str() + lb + 1);
        int size = type == "float4x4" ? 16 : type == "float4" ? 4 : type == "float3" ? 3 : type == "float2" ? 2 : type == "float" ? 1 : -1;
        if (size < 0) return -1;
        if (count > 1 || size == 16) {                       // arrays and matrices: whole rows
            off = (off + 3) / 4 * 4;
            const int rowsPer = (size + 3) / 4;
            off += (count - 1) * rowsPer * 4 + size;
            continue;
        }
        if (off / 4 != (off + size - 1) / 4) off = (off + 3) / 4 * 4;   // would straddle a row
        off += size;
    }
    return (off + 3) / 4 * 4;
}
std::string CBufferBody(const std::string& src, const char* name) {
    const std::string head = std::string("cbuffer ") + name + " ";
    size_t a = src.find(head);
    if (a == std::string::npos) return "";
    a = src.find('{', a);
    const size_t b = src.find("};", a);
    return src.substr(a + 1, b - a - 1);
}
bool DeclaredBeforeUse(const std::string& src, const std::string& fn) {
    const size_t def = src.find("float " + fn + "(");
    const size_t def3 = src.find("float3 " + fn + "(");
    size_t d = std::min(def, def3);
    if (d == std::string::npos) return false;
    const size_t use = src.find(fn + "(");            // first mention of fn( anywhere
    return use >= d;                                   // the first mention is the definition
}
}

int main() {
    RUN_SUITE("d3d11_layout");
    const std::string hlsl = D3D11HlslSource();
    CHECK(!hlsl.empty());

    // Constant buffer sizes: shader == C++ struct (so a field added on one side only fails here).
    const int cb = PackedFloats(CBufferBody(hlsl, "CB")), sh = PackedFloats(CBufferBody(hlsl, "SH")), pc = PackedFloats(CBufferBody(hlsl, "PC"));
    CHECK(cb > 0 && sh > 0 && pc > 0);
    CHECK(cb * 4 == (int)sizeof(d3d11layout::CB));
    CHECK(sh * 4 == (int)sizeof(d3d11layout::ShadowCB));
    CHECK(pc * 4 == (int)sizeof(d3d11layout::PostCB));
    CHECK(sizeof(d3d11layout::CB) % 16 == 0 && sizeof(d3d11layout::ShadowCB) % 16 == 0 && sizeof(d3d11layout::PostCB) % 16 == 0);
    // The light array: 16 lights x 6 rows, matching the C++ array and the stride the shader indexes with.
    CHECK(hlsl.find("float4 uLights[96]") != std::string::npos);
    CHECK(sizeof(d3d11layout::CB::lights) == 96 * 4 * sizeof(float));
    CHECK(hlsl.find("uLights[li*6+5]") != std::string::npos && hlsl.find("uLights[li*4") == std::string::npos);
    CHECK(sizeof(d3d11layout::ShadowCB::sunExtra) == 16);

    // Text sanity.
    int open = 0, close = 0, po = 0, pcl = 0, bo = 0, bc = 0;
    for (char c : hlsl) { open += c == '{'; close += c == '}'; po += c == '('; pcl += c == ')'; bo += c == '['; bc += c == ']'; }
    CHECK(open == close && po == pcl && bo == bc);
    // Helpers of the lighting upgrade exist once and are defined before their first use.
    for (const char* fn : {"softClip", "softClip3", "distFade", "lightAtten", "rectTerm", "cookieSample", "cookieAt"}) {
        CHECK(hlsl.find(std::string("float ") + fn + "(") != std::string::npos || hlsl.find(std::string("float3 ") + fn + "(") != std::string::npos);
        CHECK(DeclaredBeforeUse(hlsl, fn));
    }
    // Everything the new code reads is declared.
    for (const char* sym : {"uCookieTex : register(t8)", "float4 uSunExtra", "uShadowTex.Load(", "uCookieTex.SampleLevel(", "uCasInfo[ci].y"})
        CHECK(hlsl.find(sym) != std::string::npos);
    // Shader model 5.0 only: no GLSL names, no shader-model-6 wave or mesh-shader features, no two-argument atan misuse.
    for (const char* bad : {"vec2", "vec3", "vec4", "mat4", "mix(", "fract(", "gl_", "texture2D(", "shadow2D(", "WaveActive", "SV_DispatchMesh", "dot4add"})
        CHECK(hlsl.find(bad) == std::string::npos);
    // The new texture reads are legal for a typeless-depth SRV (R24_UNORM_X8): Load with an int3 location.
    CHECK(hlsl.find("Load(int3((int2)ap, 0)).r") != std::string::npos);
    // The light-mask atlas row count and the shader agree (the shader divides by uSunExtra.z, filled from kCookieRows).
    CHECK(d3d11layout::kCookieRows == 8);
    TEST_MAIN_RESULT();
}
