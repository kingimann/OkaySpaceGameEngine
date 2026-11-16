// A built game loads its textures, fonts and sounds from relative paths when it is
// started from the folder of its exe (#888 finding 45).
//
// Build Game copies Assets/checker.png to <Name>_Data/Assets/checker.png, and the scene
// still says "Assets/checker.png". Double-clicking the exe makes the exe's folder the
// working directory, not <Name>_Data. The player hands the data folder to
// Paths::SetAssetBaseDir (textures and fonts) and to LoadSceneAudio (sounds), and each
// loader tries a relative path as written first and then under that folder.
//
// This test lays out a data folder, moves the working directory somewhere else, sets the
// base folder the way the player does, and loads all three through the engine's loaders (and a picture's
// .okaymeta import settings, which Build Game ships beside it).
// It also checks that a file that opens from the working directory still wins, that an
// absolute path still works, and that a sound found on the second try logs no warning.
#include "test_framework.hpp"
#include "okay/Audio/AudioLoad.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Paths.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Graphics/TtfFont.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include <Okay.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> g_logged;

int Count(const std::string& needle) {
    int n = 0;
    for (const std::string& m : g_logged) if (m.find(needle) != std::string::npos) ++n;
    return n;
}

bool WriteSolidPng(const fs::path& file, int size, const Color& c) {
    Image img(size, size);
    img.Fill(c);
    return img.SavePNG(file.string());
}

bool WriteText(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
    return static_cast<bool>(out);
}

// Moves the working directory for the test and puts it back afterwards, even on an early return.
struct CwdGuard {
    fs::path old;
    explicit CwdGuard(const fs::path& to) { old = fs::current_path(); fs::current_path(to); }
    ~CwdGuard() { std::error_code ec; fs::current_path(old, ec); }
};

} // namespace

int main() {
    RUN_SUITE("export asset base folder");

    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / ("okay_export_asset_base_" + std::to_string(static_cast<unsigned long long>(std::chrono::system_clock::now().time_since_epoch().count())));
    const fs::path data = root / "Game_Data";      // what Build Game makes beside the exe
    const fs::path exeDir = root / "game";          // the exe's folder: the working directory on a double-click
    fs::create_directories(data / "Assets", ec);
    fs::create_directories(exeDir / "Assets", ec);
    const std::string dataDir = fs::absolute(data).generic_string() + "/";   // FindDataDir's form: ends in '/'

    // ---- the data folder ----
    CHECK(WriteSolidPng(data / "Assets" / "checker.png", 4, Color(1, 0, 0, 1)));
    // Its import settings (Build Game ships the .okaymeta beside the picture): sampled with Point filtering, clamped.
    CHECK(WriteText(data / "Assets" / "checker.png.okaymeta", "importer texture\nset filter point\nset wrap clamp\n"));
    CHECK(AudioClip::Sine(440.0f, 0.10f, 0.5f, 44100).SaveWAV((data / "Assets" / "tone.wav").string()));
    // A file in both places: the copy beside the working directory must win.
    CHECK(WriteSolidPng(data / "Assets" / "both.png", 4, Color(1, 0, 0, 1)));
    CHECK(WriteSolidPng(exeDir / "Assets" / "both.png", 2, Color(0, 1, 0, 1)));
    CHECK(AudioClip::Sine(440.0f, 0.10f, 0.5f, 44100).SaveWAV((data / "Assets" / "both.wav").string()));
    CHECK(AudioClip::Sine(440.0f, 0.30f, 0.5f, 44100).SaveWAV((exeDir / "Assets" / "both.wav").string()));

    // The font: the repository's own DejaVu Sans (docs/THIRD_PARTY.md: free font licence, text in
    // assets/fonts/DejaVuSans-LICENSE.txt). Skipped with a note when the checkout does not have it.
    const fs::path fontSrc = fs::path(OKAY_SOURCE_DIR) / "assets" / "fonts" / "DejaVuSans.ttf";
    const bool haveFont = fs::exists(fontSrc, ec);
    if (haveFont) {
        fs::copy_file(fontSrc, data / "Assets" / "ui.ttf", fs::copy_options::overwrite_existing, ec);
        CHECK(!ec);
        // In both places, the data folder's copy is not a font: loading works only if the working directory's copy wins.
        CHECK(WriteText(data / "Assets" / "both.ttf", "not a font file"));
        fs::copy_file(fontSrc, exeDir / "Assets" / "both.ttf", fs::copy_options::overwrite_existing, ec);
        CHECK(!ec);
    } else {
        std::printf("[export_asset_base] %s not found: the font checks are skipped\n", fontSrc.string().c_str());
    }

    const Log::Sink oldSink = Log::sink;
    Log::sink = [](Log::Level, const std::string& msg) { g_logged.push_back(msg); };
    {
        CwdGuard cwd(exeDir);

        // Before the player sets the folder, a relative path opens from the working directory only.
        Paths::SetAssetBaseDir("");
        AudioBaseDir().clear();
        {
            Image img;
            CHECK(!img.Load("Assets/checker.png"));
        }

        // What the player does after FindDataDir.
        Paths::SetAssetBaseDir(dataDir);
        Scene scene("export-asset-base");
        auto* src = scene.CreateGameObject("Speaker")->AddComponent<AudioSource>();
        src->clipPath = "Assets/tone.wav";
        g_logged.clear();
        std::vector<std::string> failed;
        CHECK(LoadSceneAudio(scene, dataDir, 44100, &failed) == 1);   // sets AudioBaseDir() as the player does
        CHECK(failed.empty());
        // The clip loads on the second try (under the data folder): no "cannot load audio" warning.
        CHECK(Count("cannot load audio") == 0);

        // ---- textures ----
        g_logged.clear();
        {
            Image img;
            CHECK(img.Load("Assets/checker.png"));
            CHECK(img.Width() == 4 && img.Height() == 4);
        }
        Image* tex = GetCachedTexture("Assets/checker.png");
        CHECK(tex != nullptr);
        if (tex) {
            CHECK(tex->Width() == 4);
            const Color c = tex->GetPixel(1, 1);
            CHECK(c.r > 0.9f && c.g < 0.1f && c.b < 0.1f);
        }
        CHECK(Count("cannot load image") == 0);
        // The picture's import settings are read from beside the file that loaded (every renderer asks this).
        const TextureSampling ts = TextureImportSampling("Assets/checker.png");
        CHECK(ts.filter == TextureSampling::Point);
        CHECK(ts.wrap == TextureSampling::Clamp);
        // The working directory's copy wins over the data folder's.
        Image* both = GetCachedTexture("Assets/both.png");
        CHECK(both != nullptr && both->Width() == 2);
        // An absolute path still works.
        const std::string absPng = (data / "Assets" / "checker.png").string();
        Image* absTex = GetCachedTexture(absPng);
        CHECK(absTex != nullptr && absTex->Width() == 4);
        // Missing everywhere: one warning that names the path as the scene wrote it.
        g_logged.clear();
        {
            Image img;
            CHECK(!img.Load("Assets/missing.png"));
        }
        CHECK(Count("cannot load image 'Assets/missing.png'") == 1);
        CHECK(GetCachedTexture("Assets/missing.png") == nullptr);

        // ---- fonts ----
        if (haveFont) {
            g_logged.clear();
            TtfFont* font = GetFont("Assets/ui.ttf");
            CHECK(font != nullptr && font->Valid());
            CHECK(Count("cannot open") == 0);
            TtfFont direct;
            CHECK(direct.LoadFromFile("Assets/ui.ttf", 24.0f));
            CHECK(direct.Valid());
            TtfFont fromCwd;
            CHECK(fromCwd.LoadFromFile("Assets/both.ttf", 24.0f));   // the cwd copy is the real font
            TtfFont abs;
            CHECK(abs.LoadFromFile((data / "Assets" / "ui.ttf").string(), 24.0f));
            g_logged.clear();
            TtfFont missing;
            CHECK(!missing.LoadFromFile("Assets/missing.ttf", 24.0f));
            CHECK(Count("cannot open Assets/missing.ttf") == 1);
        }

        // ---- audio ----
        g_logged.clear();
        auto clip = LoadAudioFile("Assets/tone.wav", 44100);
        CHECK(clip != nullptr);
        CHECK(Count("cannot load audio") == 0);
        auto stream = OpenAudioStream("Assets/tone.wav");
        CHECK(stream != nullptr);
        CHECK(ResolveAudioPath("Assets/tone.wav") == dataDir + "Assets/tone.wav");
        // The working directory's copy wins (0.3 s against 0.1 s in the data folder).
        auto bothClip = LoadAudioFile("Assets/both.wav", 44100);
        CHECK(bothClip != nullptr && bothClip->Duration() > 0.25f);
        // An absolute path still works.
        CHECK(LoadAudioFile((data / "Assets" / "tone.wav").string(), 44100) != nullptr);
        // Missing everywhere: one warning, not one per try.
        g_logged.clear();
        CHECK(LoadAudioFile("Assets/missing.wav", 44100) == nullptr);
        CHECK(Count("cannot load audio") == 1);
    }
    Log::sink = oldSink;
    Paths::SetAssetBaseDir("");
    AudioBaseDir().clear();
    AudioClip::ClearCache();

    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
