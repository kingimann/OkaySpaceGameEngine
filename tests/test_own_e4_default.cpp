// Compile-time default for .ogex and .mdl: when the build was configured with -DOKAY_OWN_OPENGEX=ON / -DOKAY_OWN_MDL=ON
// (tests/CMakeLists.txt then defines OKAY_EXPECT_OWN_OPENGEX / OKAY_EXPECT_OWN_MDL for this test), a file imported with the
// Default backend and no run-time override must be read by our own reader. This fails if the engine did not get
// OKAY_OWN_<FMT>_DEFAULT (for example when the name is dropped from the format list in engine/CMakeLists.txt). With the option OFF
// the test only checks that the build did not turn the default on by accident. Modelled on test_own_ter_default.cpp.
#include "test_framework.hpp"
#include "e4_fixtures.hpp"
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
    RUN_SUITE("own_e4_default");
    Unset("OKAY_OWN_OPENGEX");
    Unset("OKAY_OWN_MDL");
    const fs::path dir = fs::temp_directory_path() / "okay_own_e4_default";
    fs::create_directories(dir);
    Write(dir / "t.ogex", e4::OgexQuad());
    Write(dir / "q.mdl", e4::QuakeMdl());
    Write(dir / "h.mdl", e4::HalfLifeMdl());
    {
        ImportedModel m; std::string err;
        const bool ok = ImportModelData((dir / "t.ogex").string(), m, ImportBackend::Default, false, &err);
#ifdef OKAY_EXPECT_OWN_OPENGEX
        if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
        CHECK(ok && m.backend == "own" && !m.parts.empty());
#else
        CHECK(!ok || m.backend != "own");   // default OFF: not read by our reader unless asked
#endif
    }
    for (const char* f : {"q.mdl", "h.mdl"}) {
        ImportedModel m; std::string err;
        const bool ok = ImportModelData((dir / f).string(), m, ImportBackend::Default, false, &err);
#ifdef OKAY_EXPECT_OWN_MDL
        if (!ok) std::fprintf(stderr, "  %s: %s\n", f, err.c_str());
        CHECK(ok && m.backend == "own" && !m.parts.empty());
#else
        CHECK(!ok || m.backend != "own");
#endif
    }
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
