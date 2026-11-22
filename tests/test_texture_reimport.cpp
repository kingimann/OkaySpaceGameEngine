// A picture changed on disk, or reimported from the Project panel, is shown changed in the Scene view.
// Before this, the Project panel's thumbnail was made again but the renderer's texture cache (keyed by whatever path
// the MeshRenderer or Sprite names, often relative) kept the old pixels until the editor was restarted.
//   InvalidateTextureCacheForFile   drops every cache key that names the same file (relative, "./", absolute, other
//                                   separators), leaves "@" and "proc:" pictures alone, and bumps the generation so the
//                                   GPU renderers upload again
//   Browser::TakeChangedFiles       the files a scan found changed, plus every file reimported by hand, each once
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include "../editor/ProjectBrowser.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace okay;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_texreimp_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::create_directories(path / "Assets" / "Art", ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string assets() const { return (path / "Assets").generic_string(); }
    std::string at(const std::string& rel) const { return (path / "Assets" / rel).generic_string(); }
};
void WritePng(const std::string& p, Color c, int size = 4) {
    Image img(size, size);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) img.SetPixel(x, y, c);
    img.SavePNG(p);
}
bool IsRed(const Image* i) { return i && i->GetPixel(0, 0).r > 0.9f && i->GetPixel(0, 0).g < 0.1f; }
bool IsGreen(const Image* i) { return i && i->GetPixel(0, 0).g > 0.9f && i->GetPixel(0, 0).r < 0.1f; }
bool Contains(const std::vector<std::string>& v, const std::string& p) {
    const fs::path want = fs::path(p).lexically_normal();
    for (const auto& s : v) if (fs::path(s).lexically_normal() == want) return true;
    return false;
}
} // namespace

int main() {
    RUN_SUITE("the texture cache forgets a changed file under every name it was loaded by");
    {
        TempDir d;
        const std::string abs = d.at("Art/wall.png");
        WritePng(abs, Color(1, 0, 0, 1));
        std::error_code ec;
        const fs::path oldCwd = fs::current_path(ec);
        fs::current_path(d.path, ec);
        const std::string rel = "Assets/Art/wall.png", dotRel = "./Assets/Art/../Art/wall.png";
        CHECK(IsRed(GetCachedTexture(rel)));
        CHECK(IsRed(GetCachedTexture(dotRel)));
        CHECK(IsRed(GetCachedTexture(abs)));
        CHECK(GetCachedMips(rel) != nullptr);
        RegisterTexture("@keep", Image(2, 2));
        WritePng(abs, Color(0, 1, 0, 1));                       // painted green in another program
        CHECK(IsRed(GetCachedTexture(rel)));                    // the cache still has the old pixels (the bug, before a reload)
        const unsigned gen = TextureCacheGeneration();
        const int dropped = InvalidateTextureCacheForFile(abs);
        CHECK(dropped == 3);                                    // rel, dotRel and abs; the mip chain goes too (not counted)
        CHECK(TextureCacheGeneration() != gen);                 // the GL / D3D renderers upload again
        CHECK(IsGreen(GetCachedTexture(rel)));
        CHECK(IsGreen(GetCachedTexture(dotRel)));
        CHECK(IsGreen(GetCachedTexture(abs)));
        CHECK(TextureCacheMap().count("@keep") == 1);           // a picture registered by name has no file: kept
        // another file is not touched, and asking for a file nobody loaded drops nothing and does not bump the generation
        const std::string other = d.at("Art/floor.png");
        WritePng(other, Color(1, 0, 0, 1));
        CHECK(IsRed(GetCachedTexture(other)));
        const unsigned gen2 = TextureCacheGeneration();
        CHECK(InvalidateTextureCacheForFile(d.at("Art/nothing.png")) == 0);
        CHECK(TextureCacheGeneration() == gen2);
        CHECK(InvalidateTextureCacheForFile(abs) == 3);
        CHECK(TextureCacheMap().count(other) == 1);
        CHECK(InvalidateTextureCacheForFile("") == 0);
        fs::current_path(oldCwd, ec);
    }

    RUN_SUITE("the Project panel hands out the files that changed on disk, once each");
    {
        TempDir d;
        WritePng(d.at("Art/hero.png"), Color(1, 0, 0, 1));
        WritePng(d.at("Art/rock.png"), Color(1, 0, 0, 1));
        okaypb::Browser b(d.assets());
        CHECK(b.Sync());
        std::uint64_t t = 10000000;
        b.Poll(t);
        CHECK(b.TakeChangedFiles().empty());                    // opening the project changed nothing: nothing to reload
        WritePng(d.at("Art/hero.png"), Color(0, 1, 0, 1));      // changed in another program
        t += 10000000;
        CHECK(b.Poll(t));
        std::vector<std::string> ch = b.TakeChangedFiles();
        CHECK(ch.size() == 1 && Contains(ch, d.at("Art/hero.png")));
        CHECK(b.TakeChangedFiles().empty());                    // handed out once
        // Reimport by hand: the file is reloaded even when its bytes are the same (the person asked)
        CHECK(b.Reimport(d.at("Art/rock.png")));
        ch = b.TakeChangedFiles();
        CHECK(ch.size() == 1 && Contains(ch, d.at("Art/rock.png")));
        // Reimport All on the folder: every file in it, each once
        CHECK(b.ReimportFolder(d.at("Art")) == 2);
        ch = b.TakeChangedFiles();
        CHECK(ch.size() == 2 && Contains(ch, d.at("Art/hero.png")) && Contains(ch, d.at("Art/rock.png")));
        // new import settings (Largest side) reload the picture too
        okay::ImportSettings s;
        CHECK(b.GetImportSettings(d.at("Art/hero.png"), s));
        s.texture.maxSize = 64;
        CHECK(b.SetImportSettings(d.at("Art/hero.png"), s));
        ch = b.TakeChangedFiles();
        CHECK(Contains(ch, d.at("Art/hero.png")));
        CHECK(b.TakeChangedFiles().empty());
    }

    RUN_SUITE("end to end: a picture repainted on disk is new in the renderer after the next poll");
    {
        TempDir d;
        const std::string abs = d.at("Art/sign.png");
        WritePng(abs, Color(1, 0, 0, 1));
        okaypb::Browser b(d.assets());
        CHECK(b.Sync());
        std::uint64_t t = 10000000;
        b.Poll(t);
        b.TakeChangedFiles();
        CHECK(IsRed(GetCachedTexture(abs)));
        WritePng(abs, Color(0, 1, 0, 1));
        t += 10000000;
        b.Poll(t);
        int dropped = 0;
        for (const std::string& f : b.TakeChangedFiles()) dropped += InvalidateTextureCacheForFile(f);   // what the editor does each frame
        CHECK(dropped == 1);
        CHECK(IsGreen(GetCachedTexture(abs)));
    }
    TEST_MAIN_RESULT();
}
