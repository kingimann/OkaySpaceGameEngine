// The Direct3D 11 renderer's HLSL (engine/src/Render/D3D11Hlsl.hpp), checked without a GPU.
//
//   test_d3d11_shaders            source checks on every host: the entry points are all there,
//                                 and the SSAO shaders keep the constants and the shape of the
//                                 OpenGL renderer's SSAO (the look parity the docs promise).
//   test_d3d11_shaders --compile  compiles EVERY entry point: with D3DCompile (the compiler the
//                                 renderer itself uses) on Windows, with glslang's HLSL front end
//                                 elsewhere when `glslangValidator` is installed. Exit 77 (ctest:
//                                 skipped) when there is no compiler to ask.
//
// This does NOT run the shaders: nothing here proves how SSAO looks on a Direct3D 11 device.
#include "test_framework.hpp"
#include "Render/D3D11Hlsl.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <string>

#if defined(_WIN32)
#include <d3dcompiler.h>
#endif

namespace {
namespace hl = okay::d3d11hlsl;

bool Has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

int SourceChecks() {
    const std::string hlsl = hl::kSource;
    CHECK(hlsl.size() > 5000);
    // Every entry point exists, once, with its stage's return type.
    for (const hl::Entry& e : hl::kEntries) {
        const std::string name = e.name;
        // Defined exactly once: a line that starts "<return type> NAME(" (calls from other entry points do not).
        const std::regex def("\\n[A-Za-z0-9_]+ " + name + "\\(");
        CHECK(std::distance(std::sregex_iterator(hlsl.begin(), hlsl.end(), def), std::sregex_iterator()) == 1);
    }
    // The SSAO entry points are part of the list the renderer compiles.
    bool aoDepth = false, aoPs = false, aoSkin = false;
    for (const hl::Entry& e : hl::kEntries) {
        aoDepth |= std::string(e.name) == "VSAoDepth"; aoPs |= std::string(e.name) == "PSAo"; aoSkin |= std::string(e.name) == "VSAoDepthSkin";
    }
    CHECK(aoDepth && aoPs && aoSkin);
    // Constant-buffer registers the C++ side binds: b0 CB, b1 bones, b2 SH, b3 PC; t8 is the AO factor.
    CHECK(Has(hlsl, "cbuffer PC : register(b3)"));
    CHECK(Has(hlsl, "Texture2D uSsaoTex : register(t8)"));
    CHECK(!Has(hlsl, "register(t8)") || hlsl.find("register(t8)") == hlsl.rfind("register(t8)"));   // nothing else claims t8

    // The constants the HLSL hard-codes equal the OpenGL renderer's.
    CHECK(hl::kAoFar == 150.0f);
    CHECK(Has(hlsl, "kAoFar = 150.0;"));
    CHECK(hl::kAoSamples == 12);
    CHECK(Has(hlsl, "k < 12"));

    const std::string gl = ReadFile(std::string(OKAY_TEST_SOURCE_DIR) + "/engine/src/Render/GLRenderer.cpp");
    CHECK(gl.size() > 1000);
    CHECK(Has(gl, "kSsaoFar = 150.0f"));
    CHECK(Has(gl, "i < 12"));
    // The look: each tuning number of the GL SSAO fragment shader appears in the HLSL too.
    const char* kParity[] = {
        "2.618034",              // golden-angle spiral step
        "0.25 + 0.75",           // sample ring radii
        "/ 11.0",                // spread over the 12 samples
        "0.995",                 // sky / background
        "0.004, 0.12",           // screen-space radius clamp
        "0.03",                  // minimum occluder depth difference (bias)
        "1.6",                   // occluder range as a multiple of the radius
        "occ / 12.0",
        "0.5)",                  // minimum distance clamp max(.., 0.5)
    };
    for (const char* t : kParity) { CHECK(Has(gl, t)); CHECK(Has(hlsl, t)); }
    // The packed 16-bit depth encoding is the same.
    CHECK(Has(gl, "floor(d * 255.0) / 255.0") && Has(hlsl, "floor(d * 255.0) / 255.0"));
    CHECK(Has(gl, "fract(d * 255.0)") && Has(hlsl, "frac(d * 255.0)"));
    CHECK(Has(gl, "t.r + t.g / 255.0") && Has(hlsl, "t.r + t.g / 255.0"));

    // The shared settings resolution both GPU renderers use.
    using PS = okay::Scene::RenderSettings::PostSettings;
    okay::SSAOEnabled() = false; okay::SSAORadius() = 0.45f; okay::SSAOStrength() = 0.6f;
    PS ps;
    ps.ssao = false;
    okay::SsaoParams a = okay::ResolveSsao(ps);
    CHECK(!a.on); CHECK_NEAR(a.radius, 0.45f, 1e-6f); CHECK_NEAR(a.strength, 0.6f, 1e-6f);
    okay::SSAOEnabled() = true; okay::SSAORadius() = 0.9f; okay::SSAOStrength() = 1.7f;   // the Lighting switch, an over-range strength
    a = okay::ResolveSsao(ps);
    CHECK(a.on); CHECK_NEAR(a.radius, 0.9f, 1e-6f); CHECK_NEAR(a.strength, 1.0f, 1e-6f);   // capped: a multiply cannot brighten
    ps.ssao = true; ps.ssaoRadius = 0.3f; ps.ssaoIntensity = 0.5f;                          // the scene's post-process switch wins
    a = okay::ResolveSsao(ps);
    CHECK(a.on); CHECK_NEAR(a.radius, 0.3f, 1e-6f); CHECK_NEAR(a.strength, 0.5f, 1e-6f);
    okay::SSAOEnabled() = false;
    a = okay::ResolveSsao(ps);
    CHECK(a.on);                                                                            // on from the scene alone
    okay::SSAOEnabled() = false; okay::SSAORadius() = 0.45f; okay::SSAOStrength() = 0.6f;
    TEST_MAIN_RESULT();
}

#if defined(_WIN32)
int CompileChecks() {
    const std::string hlsl = hl::kSource;
    for (const hl::Entry& e : hl::kEntries) {
        ID3DBlob* blob = nullptr; ID3DBlob* err = nullptr;
        const HRESULT hr = D3DCompile(hlsl.data(), hlsl.size(), "okay3d", nullptr, nullptr, e.name, e.profile, 0, 0, &blob, &err);
        if (FAILED(hr)) std::cerr << "  " << e.name << ": " << (err ? (const char*)err->GetBufferPointer() : "failed") << "\n";
        CHECK(SUCCEEDED(hr) && blob && blob->GetBufferSize() > 0);
        if (blob) blob->Release();
        if (err) err->Release();
    }
    std::cout << "compiled " << (sizeof(hl::kEntries) / sizeof(hl::kEntries[0])) << " entry points with D3DCompile\n";
    TEST_MAIN_RESULT();
}
#else
int CompileChecks() {
    if (std::system("glslangValidator --version >/dev/null 2>&1") != 0) {
        std::cout << "SKIP: glslangValidator is not installed (it is only used to check the HLSL on non-Windows hosts)\n";
        return 77;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / ("okay_d3d11_shaders_" + std::to_string((long long)std::time(nullptr)) + "_" + std::to_string(std::rand()));
    fs::create_directories(dir, ec);
    const fs::path src = dir / "okay3d.hlsl";
    { std::ofstream f(src, std::ios::binary); f << hl::kSource; }
    for (const hl::Entry& e : hl::kEntries) {
        const bool vs = std::strncmp(e.profile, "vs", 2) == 0;
        const fs::path log = dir / (std::string(e.name) + ".log");
        // -V -o <spv> makes glslang run the full HLSL front end for this entry point (type and semantic
        // checks, entry point lookup); the SPIR-V itself is thrown away.
        const std::string cmd = "glslangValidator -V -D -e " + std::string(e.name) + " -S " + (vs ? "vert" : "frag") +
                                " -o \"" + (dir / (std::string(e.name) + ".spv")).string() + "\" \"" + src.string() + "\" > \"" + log.string() + "\" 2>&1";
        const int rc = std::system(cmd.c_str());
        const std::string out = ReadFile(log.string());
        if (rc != 0 || Has(out, "ERROR") || Has(out, "Entry point not found")) std::cerr << "  " << e.name << ":\n" << out << "\n";
        CHECK(rc == 0);
        CHECK(!Has(out, "ERROR"));
        CHECK(!Has(out, "Entry point not found"));
    }
    std::cout << "compiled " << (sizeof(hl::kEntries) / sizeof(hl::kEntries[0])) << " entry points with glslang's HLSL front end\n";
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
#endif
} // namespace

int main(int argc, char** argv) {
    RUN_SUITE("d3d11_shaders");
    if (argc > 1 && std::strcmp(argv[1], "--compile") == 0) return CompileChecks();
    return SourceChecks();
}
