// Build Settings > Development build: the dev log file. A release build writes none; a
// development build writes <game>.log with timestamps: startup, scene loads, script
// warnings/errors, asset load failures and frame-time hitches.
#include "test_framework.hpp"
#include "DevLog.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Scene/Scene.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace okay;
namespace fs = std::filesystem;

static std::string Slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "okay_devlog_test";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);
    const std::string exe = (dir / "MyGame.exe").string();
    const std::string path = devlog::LogPathFor(exe, "");
    CHECK(fs::path(path).filename() == "MyGame.log");
    CHECK(fs::path(path).parent_path() == dir);
    CHECK(devlog::LogPathFor("game", "/base/") == "/base/game.log");

    Log::console = false;
    // Release build: no file, nothing active.
    CHECK(!devlog::Start(false, path, "", "My Game", "", "scenes/a.okayscene"));
    Log::Warning("release warning");
    CHECK(!fs::exists(path) && !devlog::Active());

    // Development build: the key flips it on.
    CHECK(devlog::Start(true, path, "", "My Game", "", "scenes/a.okayscene"));
    CHECK(devlog::Active() && fs::exists(path));
    Log::Warning("[script] something odd");
    Log::Error("OkayScript Update(): boom");
    { std::string e; Image img; CHECK(!img.Load((dir / "missing.png").string(), &e)); }
    {   // a scene that cannot be loaded
        Scene sc("x");
        sc.RequestLoad((dir / "nope.okayscene").string());
        sc.Update(0.016f);
    }
    {   // a hitch: only a long frame well over the average
        devlog::HitchWatch w; w.warmup = 5;
        bool any = false;
        for (int i = 0; i < 30; ++i) any |= w.Frame(0.016);
        CHECK(!any);
        CHECK(w.Frame(0.5));
        CHECK(w.Hitches() == 1);
    }
    devlog::Stop();
    Log::console = true;
    CHECK(!devlog::Active());

    const std::string text = Slurp(path);
    CHECK(text.find("development build started: My Game") != std::string::npos);
    CHECK(text.find("first scene: scenes/a.okayscene") != std::string::npos);
    CHECK(text.find("[script] something odd") != std::string::npos);
    CHECK(text.find("OkayScript Update(): boom") != std::string::npos);
    CHECK(text.find("[asset] cannot load image") != std::string::npos);
    CHECK(text.find("[scene] could not load") != std::string::npos);
    CHECK(text.find("[perf] frame hitch: 500 ms") != std::string::npos);
    CHECK(text.find("release warning") == std::string::npos);
    CHECK(text.find("shutting down") != std::string::npos);
    // timestamped: HH:MM:SS.mmm at the start of the line
    CHECK(text.size() > 12 && text[2] == ':' && text[5] == ':' && text[8] == '.');

    fs::remove_all(dir, ec);
    std::cout << okaytest::g_checks << " checks, " << okaytest::g_failures << " failures\n";
    return okaytest::g_failures == 0 ? 0 : 1;
}
