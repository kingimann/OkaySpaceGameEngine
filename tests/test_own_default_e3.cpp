// Compile-time defaults for .iqm / .cob / .ndo: when the build was configured with -DOKAY_OWN_IQM=ON (tests/CMakeLists.txt then defines
// OKAY_EXPECT_OWN_IQM for this test), a .iqm file imported with the Default backend and no run-time override must be read by our own reader;
// likewise COB and NDO (the NDO seam is the default reader and refuses, so only EffectiveImportBackend is checked for it). This FAILS if the
// engine did not get OKAY_OWN_<FMT>_DEFAULT (for example when the name is dropped from the foreach list in engine/CMakeLists.txt, as TER once
// was). With an option OFF it only checks that the build did not turn the default on by accident.
#include "test_framework.hpp"
#include "e3_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

static void WriteFile(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary); f.write(s.data(), (std::streamsize)s.size()); }

int main() {
    RUN_SUITE("own_default_e3");
    for (const char* v : {"OKAY_OWN_IQM", "OKAY_OWN_COB", "OKAY_OWN_NDO"}) {
#ifdef _WIN32
        _putenv_s(v, "");
#else
        unsetenv(v);
#endif
    }
    const fs::path dir = fs::temp_directory_path() / "okay_own_default_e3";
    fs::create_directories(dir);
    WriteFile(dir / "t.iqm", fx::BuildIqm());
    WriteFile(dir / "t.cob", fx::CobBinary());
    {
        ImportedModel m; std::string err;
        const bool ok = ImportModelData((dir / "t.iqm").string(), m, ImportBackend::Default, false, &err);
#ifdef OKAY_EXPECT_OWN_IQM
        if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
        CHECK(ok && m.backend == "own" && !m.parts.empty() && EffectiveImportBackend(".iqm") == ImportBackend::Own);
#else
        CHECK((!ok || m.backend != "own") && EffectiveImportBackend(".iqm") != ImportBackend::Own);
#endif
    }
    {
        ImportedModel m; std::string err;
        const bool ok = ImportModelData((dir / "t.cob").string(), m, ImportBackend::Default, false, &err);
#ifdef OKAY_EXPECT_OWN_COB
        if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
        CHECK(ok && m.backend == "own" && !m.parts.empty() && EffectiveImportBackend(".cob") == ImportBackend::Own);
#else
        CHECK((!ok || m.backend != "own") && EffectiveImportBackend(".cob") != ImportBackend::Own);
#endif
    }
#ifdef OKAY_EXPECT_OWN_NDO
    CHECK(EffectiveImportBackend(".ndo") == ImportBackend::Own);
#else
    CHECK(EffectiveImportBackend(".ndo") != ImportBackend::Own);
#endif
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
