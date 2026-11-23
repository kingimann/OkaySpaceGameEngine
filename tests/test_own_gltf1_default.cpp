// Compile-time default for glTF 1.0: when the build was configured with -DOKAY_OWN_GLTF1=ON (tests/CMakeLists.txt then
// defines OKAY_EXPECT_OWN_GLTF1 for this test), a glTF 1.0 file imported with the Default backend and no run-time override
// must be read by our own reader. This fails if the engine did not get OKAY_OWN_GLTF1_DEFAULT (for example the option
// dropped from engine/CMakeLists.txt by a merge). With the option OFF it checks that the default did not turn on by accident.
// Same pattern as the coordinator's tests/test_own_ter_default.cpp.
#include "test_framework.hpp"
#include "gltf1_test_build.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

int main() {
    RUN_SUITE("own_gltf1_default");
#ifdef _WIN32
    _putenv_s("OKAY_OWN_GLTF1", "");
#else
    unsetenv("OKAY_OWN_GLTF1");
#endif
    const fs::path dir = fs::temp_directory_path() / "okay_own_gltf1_default";
    fs::create_directories(dir);
    const fs::path f = dir / "quad.gltf";
    { std::ofstream o(f, std::ios::binary); const std::string b = gltf1build::QuadJson(); o.write(b.data(), (std::streamsize)b.size()); }
    ImportedModel m; std::string err;
    const bool ok = ImportModelData(f.string(), m, ImportBackend::Default, false, &err);
#ifdef OKAY_EXPECT_OWN_GLTF1
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
