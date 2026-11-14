// Characterization of the simple loaders that cache (DataAsset::Cached, AudioClip::LoadCached,
// LootTable::Cached, RecipeBook::Cached). They share the asset manager's cache; these checks
// pin the behaviour they had before, and the last block checks the one cache.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Audio/AudioClip.hpp"
#include "okay/Core/AssetManager.hpp"
#include "okay/Core/DataAsset.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Inventory/LootTable.hpp"
#include "okay/Inventory/Recipe.hpp"
#include <filesystem>
#include <fstream>

using namespace okay;
namespace fs = std::filesystem;

static void WriteText(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "okay_asset_unify";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);

    // --- DataAsset ---
    {
        const std::string p = (dir / "a.okaydata").string();
        WriteText(p, "hp = 10\nname = orc\n");
        DataAsset& a = DataAsset::Cached(p);
        CHECK(a.GetInt("hp") == 10);
        CHECK(&DataAsset::Cached(p) == &a);              // one instance per path
        a.Set("hp", "11");
        CHECK(DataAsset::Cached(p).GetInt("hp") == 11);  // edits stay in the cache
        WriteText(p, "hp = 20\n");
        CHECK(DataAsset::Cached(p).GetInt("hp") == 11);  // a file change is not seen by itself
        CHECK(DataAsset::Reload(p));
        CHECK(&DataAsset::Cached(p) == &a);              // reload is in place
        CHECK(a.GetInt("hp") == 20);
        CHECK(!a.Has("name"));
        CHECK(!DataAsset::Reload((dir / "none.okaydata").string()));
        CHECK(a.GetInt("hp") == 20);                     // a failed reload keeps the copy
        DataAsset::Invalidate(p);
        CHECK(a.Fields().empty());                       // emptied in place, not freed
        CHECK(DataAsset::Cached(p).GetInt("hp") == 20);  // read again
        CHECK(&DataAsset::Cached(p) == &a);
        DataAsset::InvalidateAll();
        CHECK(a.Fields().empty());
        CHECK(DataAsset::Cached(p).GetInt("hp") == 20);

        // A missing file gives an empty asset to Set() into; it tries the file again while untouched.
        const std::string m = (dir / "late.okaydata").string();
        CHECK(DataAsset::Cached(m).Fields().empty());
        WriteText(m, "x = 5\n");
        CHECK(DataAsset::Cached(m).GetInt("x") == 5);
        const std::string s = (dir / "scratch.okaydata").string();
        DataAsset::Cached(s).Set("k", "v");
        WriteText(s, "k = other\n");
        CHECK(DataAsset::Cached(s).GetString("k") == "v"); // touched: never overwritten
    }

    // --- AudioClip::LoadCached ---
    {
        const std::string w = (dir / "tone.wav").string();
        AudioClip tone = AudioClip::Const(0.25f, 4410, 22050);
        CHECK(tone.SaveWAV(w));
        std::string err;
        auto a = AudioClip::LoadCached(w, 0, &err);
        auto b = AudioClip::LoadCached(w, 0, &err);
        CHECK(a && a == b);                              // same shared clip
        CHECK(a->sampleRate == 22050);
        auto c = AudioClip::LoadCached(w, 44100);
        CHECK(c && c != a && c->sampleRate == 44100);    // a rate is its own entry
        CHECK(AudioClip::LoadCached(w, 44100) == c);
        auto d = AudioClip::LoadCached(w, 0, nullptr, true);
        CHECK(d && d->channels == 1);
        CHECK(AudioClip::LoadCached((dir / "gone.wav").string(), 0, &err) == nullptr);
        CHECK(!err.empty());
        // The clip stays alive and cached while nobody else holds it.
        const AudioClip* raw = a.get();
        a.reset(); b.reset();
        CHECK(AudioClip::LoadCached(w, 0).get() == raw);
    }

    // --- LootTable::Cached and RecipeBook::Cached ---
    {
        ItemDatabase::Get().Clear();
        const std::string p = (dir / "l.okayloot").string();
        WriteText(p, "okayloot 1\nrolls 1 1\nentry \"wood\" 1 1 1 1\n");
        const LootTable& t = LootTable::Cached(p);
        CHECK(&LootTable::Cached(p) == &t);
        const std::string miss = (dir / "no.okayloot").string();
        CHECK(LootTable::Cached(miss).entries.empty());
        WriteText(miss, "okayloot 1\nentry \"wood\" 1 1 1 1\n");
        CHECK(LootTable::Cached(miss).entries.empty());   // a failure is remembered until Forget
        LootTable::Forget(miss);
        CHECK(LootTable::Cached(miss).entries.size() == 1);

        const std::string r = (dir / "r.okayrecipes").string();
        WriteText(r, "okayrecipes 1\nrecipe \"Plank\" 1\n  in \"wood\" 1\n  out \"plank\" 1\nend\n");
        const RecipeBook& rb = RecipeBook::Cached(r);
        CHECK(&RecipeBook::Cached(r) == &rb);
        const size_t n = rb.recipes.size();
        WriteText(r, "okayrecipes 1\n");
        CHECK(RecipeBook::Cached(r).recipes.size() == n);
        RecipeBook::Forget(r);
        CHECK(RecipeBook::Cached(r).recipes.empty());
        const std::string rm = (dir / "none.okayrecipes").string();
        CHECK(RecipeBook::Cached(rm).recipes.empty());
    }

    // --- One cache: the manager and the old entry points share the same object ---
    {
        const std::string p = (dir / "shared.okaydata").string();
        WriteText(p, "v = 1\n");
        DataAsset& a = DataAsset::Cached(p);
        auto h = AssetManager::Instance().Find<DataAsset>(p);
        CHECK(h && h.Get() == &a);
        auto g = AssetManager::Instance().Get<DataAsset>(p);
        CHECK(g.Get() == &a);
        // The shared audio clips and the loot tables are manager entries too.
        const std::string report = AssetManager::Instance().Report().ToString();
        CHECK(report.find("AudioClip (shared)") != std::string::npos);
        CHECK(report.find("LootTable") != std::string::npos);
        CHECK(report.find("RecipeBook") != std::string::npos);
        // Evict: the next Get decodes again, the old handle stays valid.
        AssetManager::Instance().Evict<DataAsset>(p);
        auto fresh = AssetManager::Instance().Get<DataAsset>(p);
        CHECK(fresh && fresh.Get() != &a);
        CHECK(a.GetInt("v") == 1);
        CHECK(!AssetManager::Instance().Find<DataAsset>(dir.string() + "/never.okaydata"));
    }

    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
