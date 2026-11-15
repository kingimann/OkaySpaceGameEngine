// Compile-time default for .pmx and .lws / .mot: when the build was configured with -DOKAY_OWN_PMX=ON or -DOKAY_OWN_LWS=ON
// (tests/CMakeLists.txt then defines OKAY_EXPECT_OWN_PMX / OKAY_EXPECT_OWN_LWS for this test), a file of that format imported with the
// Default backend and no run-time override must be read by our own reader. This fails if the engine did not get
// OKAY_OWN_PMX_DEFAULT / OKAY_OWN_LWS_DEFAULT (a merge once silently dropped TER from the foreach list in engine/CMakeLists.txt).
// With the option OFF the test only checks that the build did not turn the default on by accident.
#include "test_framework.hpp"
#include "pmxlws_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

static void Unset(const char* n) {
#ifdef _WIN32
    _putenv_s(n, "");
#else
    unsetenv(n);
#endif
}
static void Write(const fs::path& p, const std::string& s) { std::ofstream o(p, std::ios::binary); o.write(s.data(), (std::streamsize)s.size()); }

int main() {
    RUN_SUITE("own_pmx_lws_default");
    Unset("OKAY_OWN_PMX");
    Unset("OKAY_OWN_LWS");
    const fs::path dir = fs::temp_directory_path() / "okay_own_pmx_lws_default";
    fs::create_directories(dir);
    Write(dir / "t.pmx", fx::BuildPmx());
    Write(dir / "t.lws", "LWSC\n3\nLoadObject tri.obj\n");
    Write(dir / "t.mot", fx::BuildMot());
    Write(dir / "tri.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    {
        ImportedModel m; std::string err;
        const bool ok = ImportModelData((dir / "t.pmx").string(), m, ImportBackend::Default, false, &err);
#ifdef OKAY_EXPECT_OWN_PMX
        if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
        CHECK(ok && m.backend == "own" && !m.parts.empty());
        CHECK(EffectiveImportBackend(".pmx") == ImportBackend::Own);
#else
        CHECK(!ok || m.backend != "own");   // default OFF: not read by our reader unless asked
        CHECK(EffectiveImportBackend(".pmx") != ImportBackend::Own);
#endif
    }
    for (const char* f : {"t.lws", "t.mot"}) {
        ImportedModel m; std::string err;
        const bool ok = ImportModelData((dir / f).string(), m, ImportBackend::Default, false, &err);
#ifdef OKAY_EXPECT_OWN_LWS
        if (!ok) std::fprintf(stderr, "  %s: %s\n", f, err.c_str());
        CHECK(ok && m.backend == "own");
        CHECK(EffectiveImportBackend(std::string(".") + (f + 2)) == ImportBackend::Own);
#else
        CHECK(!ok || m.backend != "own");
        CHECK(EffectiveImportBackend(std::string(".") + (f + 2)) != ImportBackend::Own);
#endif
    }
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
