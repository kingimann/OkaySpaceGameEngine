// Compile-time default for .ogex and .mdl: when the build was configured with -DOKAY_OWN_OPENGEX=ON / -DOKAY_OWN_MDL=ON
// (tests/CMakeLists.txt then defines OKAY_EXPECT_OWN_OPENGEX / OKAY_EXPECT_OWN_MDL for this test), a file imported with the
// Default backend and no run-time override must be read by our own reader. This fails if the engine did not get
// OKAY_OWN_<FMT>_DEFAULT (for example when the name is dropped from the format list in engine/CMakeLists.txt). With the option OFF
// the test only checks that the build did not turn the default on by accident. Modelled on test_own_ter_default.cpp.
#include "test_framework.hpp"
#include "e4_fixtures.hpp"
#include "own_seam_checks.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

using seam::Write;

#ifdef OKAY_EXPECT_OWN_OPENGEX
static const bool kOgexOwn = true;
#else
static const bool kOgexOwn = false;
#endif
#ifdef OKAY_EXPECT_OWN_MDL
static const bool kMdlOwn = true;
#else
static const bool kMdlOwn = false;
#endif

int main() {
    RUN_SUITE("own_e4_default");
    seam::UnsetEnv("OKAY_OWN_OPENGEX");
    seam::UnsetEnv("OKAY_OWN_MDL");
    const fs::path dir = fs::temp_directory_path() / "okay_own_e4_default";
    fs::create_directories(dir);
    Write(dir / "t.ogex", e4::OgexQuad());
    Write(dir / "q.mdl", e4::QuakeMdl());
    Write(dir / "h.mdl", e4::HalfLifeMdl());
    seam::CheckDefaultBackend((dir / "t.ogex").string(), kOgexOwn);
    for (const char* f : {"q.mdl", "h.mdl"}) seam::CheckDefaultBackend((dir / f).string(), kMdlOwn);
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
