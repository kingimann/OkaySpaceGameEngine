// Compile-time default for .ter: when the build was configured with -DOKAY_OWN_TER=ON (tests/CMakeLists.txt then defines
// OKAY_EXPECT_OWN_TER for this test), a .ter file imported with the Default backend and no run-time override must be read by
// our own reader. This fails if the engine did not get OKAY_OWN_TER_DEFAULT (e.g. the option dropped from engine/CMakeLists.txt).
// With the option OFF the test only checks that the build did not turn the default on by accident.
#include "test_framework.hpp"
#include "terragen_test_build.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
using namespace terbuild;
namespace fs = std::filesystem;

int main() {
    RUN_SUITE("own_ter_default");
#ifdef _WIN32
    _putenv_s("OKAY_OWN_TER", "");
#else
    unsetenv("OKAY_OWN_TER");
#endif
    const fs::path dir = fs::temp_directory_path() / "okay_own_ter_default";
    fs::create_directories(dir);
    const fs::path f = dir / "t.ter";
    { std::ofstream o(f, std::ios::binary); const std::string b = Grid(3, 2, 16384, 10, std::vector<std::int16_t>(6, 0), 2.0f); o.write(b.data(), (std::streamsize)b.size()); }
    ImportedModel m; std::string err;
    const bool ok = ImportModelData(f.string(), m, ImportBackend::Default, false, &err);
#ifdef OKAY_EXPECT_OWN_TER
    if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
    CHECK(ok);
    CHECK(m.backend == "own");
    CHECK(!m.parts.empty());
#else
    CHECK(!ok || m.backend != "own");   // default OFF: not read by our reader unless asked
#endif
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
