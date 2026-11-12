// The Project panel's model on top of okay::AssetDatabase and okay::ThumbnailCache (editor/ProjectBrowser.*, no ImGui):
// the listing, search, filters, "used by" / "depends on" and the asset info all come from the database (no disk walk
// of the panel's own), a refresh is incremental and cheap, thumbnails are cached by GUID + content hash, kept on disk and
// made lazily inside a per-frame time budget, and rename / move / delete keep GUIDs and show broken references.
// Everything runs on temp folders; nothing sleeps. The 5,000-asset suite prints the refresh times it measured.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Audio/AudioClip.hpp"
#include "../editor/ProjectBrowser.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>

namespace fs = std::filesystem;
using namespace okaypb;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_projdb_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path / "Assets", ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string assets() const { return (path / "Assets").generic_u8string(); }
    std::string at(const std::string& rel) const { return (path / "Assets" / fs::u8path(rel)).generic_u8string(); }
};

void WriteFile(const std::string& p, const std::string& s) {
    std::error_code ec;
    fs::create_directories(fs::u8path(p).parent_path(), ec);
    std::ofstream(fs::u8path(p), std::ios::binary | std::ios::trunc) << s;
}
std::string ReadFile(const std::string& p) {
    std::ifstream in(fs::u8path(p), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
bool Has(const std::string& hay, const std::string& n) { return hay.find(n) != std::string::npos; }
bool Exists(const std::string& p) { std::error_code ec; return fs::exists(fs::u8path(p), ec); }

std::vector<std::string> Names(const std::vector<Entry>& v) { std::vector<std::string> n; for (auto& e : v) n.push_back(e.name); return n; }
bool Contains(const std::vector<Entry>& v, const std::string& name) { for (auto& e : v) if (e.name == name) return true; return false; }

double Ms(std::chrono::steady_clock::time_point a) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count(); }

// A real small PNG (so the image generator can decode it).
void WritePng(const std::string& p, int w, int h, int seed) {
    std::error_code ec;
    fs::create_directories(fs::u8path(p).parent_path(), ec);
    okay::Image img(w, h);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) img.SetPixel(x, y, okay::Color(((x + seed) % 7) / 7.0f, (y % 5) / 5.0f, 0.5f, 1.0f));
    img.SavePNG(p);
}

// textures, a material, a prefab and two scenes that use them; one scene also names a texture that does not exist.
struct Proj {
    TempDir td;
    Proj() {
        WritePng(td.at("Art/hero.png"), 64, 64, 1);
        WritePng(td.at("Art/rock.png"), 32, 32, 2);
        WritePng(td.at("Art/unused.png"), 16, 16, 3);
        WriteFile(td.at("Art/hero.okaymat"), "okaymat 1\ntexture Art/hero.png\n");
        WriteFile(td.at("Prefabs/hero.okayprefab"), "gameobject \"Hero\"\n  sprite texture \"Art/hero.png\"\n");
        WriteFile(td.at("Scenes/main.okayscene"),
                  "scene \"Main\"\ngameobject \"A\"\n  sprite texture \"Art/hero.png\"\ngameobject \"B\"\n  sprite texture \"Art/rock.png\"\n"
                  "gameobject \"C\"\n  sprite prefab \"Prefabs/hero.okayprefab\"\n");
        WriteFile(td.at("Scenes/broken.okayscene"), "scene \"Broken\"\ngameobject \"A\"\n  sprite texture \"Art/gone.png\"\n");
        WriteFile(td.at("Scripts/move.okay"), "class Move { string tex = \"Art/rock.png\"; }\n");
        WriteFile(td.at("Empty/.keep"), "");
    }
};

} // namespace

int main() {
    RUN_SUITE("the listing comes from the database and follows the folder on disk, incrementally");
    {
        Proj p;
        Browser b(p.td.assets());
        ListOptions o;
        auto root = b.List(p.td.assets(), o);
        CHECK((Names(root) == std::vector<std::string>{"Art", "Empty", "Prefabs", "Scenes", "Scripts"}));   // folders, the empty one too
        CHECK(root[0].isDir && root[0].guid.IsNull());
        auto art = b.List(p.td.at("Art"), o);
        CHECK((Names(art) == std::vector<std::string>{"hero.okaymat", "hero.png", "rock.png", "unused.png"}));
        CHECK(art[1].kind == okay::AssetType::Texture && !art[1].guid.IsNull() && !art[1].hasMeta && art[1].size > 0 && art[1].modified > 0);
        CHECK(b.AssetCount() == 8);                                       // every file is in the database, none of it written to disk
        int metas = 0;
        for (fs::recursive_directory_iterator it(p.td.path), end; it != end; ++it) if (it->path().extension() == ".okaymeta") ++metas;
        CHECK(metas == 0);

        std::uint64_t t = 10000000;
        b.Poll(t);
        const std::uint64_t rev = b.Revision();
        CHECK(!b.Poll(t + 5000000) && b.Revision() == rev);               // nothing changed: nothing is rebuilt
        CHECK(!b.Poll(t + 5000001));                                      // and it is not even due again yet

        t += 10000000;
        WritePng(p.td.at("Art/new.png"), 8, 8, 4);                        // create
        CHECK(b.Poll(t) && b.Revision() > rev);
        art = b.List(p.td.at("Art"), o);
        CHECK(Contains(art, "new.png") && art.size() == 5);

        t += 10000000;
        WritePng(p.td.at("Art/hero.png"), 64, 64, 9);                     // modify (same size, other pixels)
        std::uint64_t before = b.Info(p.td.at("Art/hero.png")).hash;
        CHECK(b.Poll(t));
        CHECK(b.Info(p.td.at("Art/hero.png")).hash != before);

        t += 10000000;
        fs::rename(fs::u8path(p.td.at("Art/rock.png")), fs::u8path(p.td.at("Art/stone.png")));   // rename outside the editor
        CHECK(b.Poll(t));
        art = b.List(p.td.at("Art"), o);
        CHECK(Contains(art, "stone.png") && !Contains(art, "rock.png"));
        CHECK(b.Db().PathToGuid("Art/stone.png") != okay::Guid());        // followed by content: same GUID

        t += 10000000;
        fs::remove(fs::u8path(p.td.at("Art/unused.png")));                // delete
        CHECK(b.Poll(t));
        CHECK(!Contains(b.List(p.td.at("Art"), o), "unused.png"));

        t += 10000000;
        fs::create_directories(fs::u8path(p.td.at("Models/Props")));      // a new empty folder is listed too
        CHECK(b.Poll(t));
        CHECK(Contains(b.List(p.td.assets(), o), "Models"));
        CHECK(Contains(b.List(p.td.at("Models"), o), "Props"));
        const auto sub = b.SubFolders(p.td.assets());
        CHECK(sub.size() == 6 && sub[0].path == p.td.at("Art") && sub[0].items == 4);
    }

    RUN_SUITE("search and filters: names, types, labels, used by, depends on");
    {
        Proj p;
        Browser b(p.td.assets());
        ListOptions o; o.recursive = true;
        auto q = [&](const std::string& text) { ListOptions x; x.text = text; return b.List(p.td.assets(), x); };
        CHECK(Names(q("hero")).size() == 3);                              // png, material, prefab
        CHECK(Contains(q("HERO"), "hero.okayprefab"));                    // any case
        CHECK((Names(q("hero t:material")) == std::vector<std::string>{"hero.okaymat"}));
        CHECK(q("ext:png").size() == 3);
        CHECK(q("t:texture").size() == 3 && q("t:scene").size() == 2 && q("t:prefab").size() == 1);
        CHECK(q("t:folder").size() == 5 && q("t:folder")[0].isDir);
        CHECK(q("zzzz").empty());
        CHECK(Contains(q("scene"), "main.okayscene") && !Contains(q("scene"), "hero.png"));          // a type word matches the type
        CHECK(Names(q("hero scene")).empty());                            // every word must match

        // labels (tags) live in the asset's meta; they are searched by l:, and as plain words
        std::string err;
        CHECK(b.AddLabel(p.td.at("Art/hero.png"), "Character", &err) && b.AddLabel(p.td.at("Art/rock.png"), "environment", &err));
        CHECK(Exists(p.td.at("Art/hero.png.okaymeta")) && Has(ReadFile(p.td.at("Art/hero.png.okaymeta")), "label Character"));
        CHECK((Names(q("l:character")) == std::vector<std::string>{"hero.png"}));
        CHECK(Names(q("char")).size() == 1 && Names(q("char"))[0] == "hero.png");
        CHECK(q("l:character l:environment").empty());                    // all of them
        CHECK(b.Info(p.td.at("Art/hero.png")).labels.size() == 1);
        CHECK(b.RemoveLabel(p.td.at("Art/hero.png"), "character") && q("l:character").empty());
        CHECK(b.Info(p.td.at("Art/hero.png")).hasMeta);                        // the meta (and so the GUID) stays on disk

        // used by / depends on
        CHECK(Names(q("uses:hero.png")).size() == 3 && Contains(q("uses:hero.png"), "hero.okaymat") && Contains(q("uses:hero.png"), "main.okayscene"));
        CHECK(Contains(q("uses:rock.png"), "move.okay"));                 // a script that names a file counts as a user
        CHECK(Contains(q("deps:main.okayscene"), "hero.png") && Contains(q("deps:main.okayscene"), "hero.okayprefab") && !Contains(q("deps:main.okayscene"), "unused.png"));
        CHECK(q("uses:nothing.png").empty());
        CHECK((Names(q("is:unused t:texture")) == std::vector<std::string>{"unused.png"}));
        CHECK((Names(q("is:broken")) == std::vector<std::string>{"broken.okayscene"}));
        ListOptions un; un.unusedOnly = true; un.recursive = true;
        CHECK(Contains(b.List(p.td.assets(), un), "unused.png") && !Contains(b.List(p.td.assets(), un), "hero.png"));
        ListOptions rec; rec.modifiedWithinSeconds = 7 * 86400;
        CHECK(b.List(p.td.assets(), rec).size() == 8);                    // all of it was just written
        rec.now = 4000000000ll;
        CHECK(b.List(p.td.assets(), rec).empty());                        // a clock far ahead: nothing is recent
        const auto& c = b.CountsNow();
        CHECK(c.total == 8 && c.byFilter[2] == 3 && c.byFilter[3] == 2 && c.byFilter[5] == 1 && c.byFilter[4] == 1 && c.byFilter[1] == 1 && c.recent == 8);
        ListOptions sorted; sorted.recursive = true; sorted.text = "ext:png"; sorted.sort = SortKey::Size; sorted.descending = true;
        auto bySize = b.List(p.td.assets(), sorted);
        CHECK(bySize.size() == 3 && bySize[0].size >= bySize[1].size && bySize[1].size >= bySize[2].size);
    }

    RUN_SUITE("find references and show dependencies come from the dependency graph");
    {
        Proj p;
        Browser b(p.td.assets());
        auto users = b.UsersOfList(p.td.at("Art/hero.png"), false);
        CHECK((std::set<std::string>(users.begin(), users.end()) ==
               std::set<std::string>{p.td.at("Art/hero.okaymat"), p.td.at("Prefabs/hero.okayprefab"), p.td.at("Scenes/main.okayscene")}));
        CHECK(b.UsersOf(p.td.at("Art/hero.png")) == 3 && b.UsersOf(p.td.at("Art/unused.png")) == 0);
        auto deps = b.DependenciesOf(p.td.at("Scenes/main.okayscene"), false);
        CHECK((std::set<std::string>(deps.begin(), deps.end()) ==
               std::set<std::string>{p.td.at("Art/hero.png"), p.td.at("Art/rock.png"), p.td.at("Prefabs/hero.okayprefab")}));
        CHECK(b.DependenciesOf(p.td.at("Scenes/main.okayscene")).size() == 3);
        CHECK(b.DependenciesOf(p.td.at("Scenes/main.okayscene"), true).size() == 3);   // the prefab's texture is already in the list
        auto prefabUsers = b.UsersOfList(p.td.at("Art/hero.png"), true);
        CHECK(prefabUsers.size() == 3);                                   // scene, prefab, material (the scene also through the prefab)
        auto refs = b.ReferencesTo({p.td.at("Art/hero.png")});
        std::set<std::string> who; for (auto& r : refs) who.insert(r.path);
        CHECK(who.size() == 3);
        CHECK(b.ReferencesTo({p.td.at("Art"), p.td.at("Prefabs"), p.td.at("Scenes")}).empty() == false);   // the script is outside
        const auto info = b.Info(p.td.at("Scenes/main.okayscene"));
        CHECK(info.indexed && info.typeName == "Scene" && info.uses == 3 && info.usesAll == 3 && info.usedBy == 0 && info.guidShort.size() == 8 && info.missing.empty());
        const auto bad = b.Info(p.td.at("Scenes/broken.okayscene"));
        CHECK(bad.missing.size() == 1 && Has(bad.missing[0], "gone.png"));
        CHECK(!b.Info(p.td.at("nope.png")).indexed);
    }

    RUN_SUITE("rename, move, delete: GUID kept, references follow, broken ones show a badge, undo");
    {
        Proj p;
        Browser b(p.td.assets());
        const okay::Guid metaGuid = b.EnsureMeta(p.td.at("Art/hero.png"));      // this one has a meta on disk
        const okay::Guid memGuid = b.Db().PathToGuid("Art/rock.png");            // this one only lives in the database
        CHECK(!metaGuid.IsNull() && !memGuid.IsNull() && b.Db().PathToGuid("Art/hero.png") == metaGuid);
        CHECK(b.Rename(p.td.at("Art/hero.png"), "champion").ok && b.Rename(p.td.at("Art/rock.png"), "stone").ok);
        CHECK(b.Db().PathToGuid("Art/champion.png") == metaGuid && b.Db().PathToGuid("Art/stone.png") == memGuid);
        CHECK(Has(ReadFile(p.td.at("Scenes/main.okayscene")), "Art/champion.png") && Has(ReadFile(p.td.at("Scenes/main.okayscene")), "Art/stone.png"));
        CHECK(b.UsersOf(p.td.at("Art/champion.png")) == 3);                      // the graph followed
        auto mv = b.Move({p.td.at("Art/champion.png"), p.td.at("Art/stone.png")}, p.td.at("Empty"));
        CHECK(mv.ok && b.Db().PathToGuid("Empty/champion.png") == metaGuid && b.Db().PathToGuid("Empty/stone.png") == memGuid);
        CHECK(Exists(p.td.at("Empty/champion.png.okaymeta")) && !Exists(p.td.at("Art/champion.png.okaymeta")));   // the meta moved with the asset
        CHECK(b.UsersOf(p.td.at("Empty/champion.png")) == 3);
        CHECK(!Contains(b.List(p.td.at("Art"), ListOptions()), "champion.png") && Contains(b.List(p.td.at("Empty"), ListOptions()), "champion.png"));   // no stale rows
        CHECK(b.Undo().ok);
        CHECK(b.Db().PathToGuid("Art/champion.png") == metaGuid && b.Db().PathToGuid("Art/stone.png") == memGuid);
        CHECK(b.Undo().ok && b.Undo().ok);
        CHECK(b.Db().PathToGuid("Art/hero.png") == metaGuid && b.Db().PathToGuid("Art/rock.png") == memGuid);
        CHECK(Has(ReadFile(p.td.at("Scenes/main.okayscene")), "Art/hero.png"));

        // delete: the scene's reference becomes broken and is badged; undo brings the file (and the GUID) back
        ListOptions lo;
        auto scenes = [&] { return b.List(p.td.at("Scenes"), lo); };
        int before = 0; for (auto& e : scenes()) before += e.missingRefs;
        CHECK(before == 1);                                               // only broken.okayscene
        CHECK(b.Trash({p.td.at("Art/hero.png")}).ok);
        int after = 0; std::map<std::string, int> badge;
        for (auto& e : scenes()) { after += e.missingRefs; badge[e.name] = e.missingRefs; }
        CHECK(badge["main.okayscene"] == 1 && badge["broken.okayscene"] == 1 && after == 2);
        CHECK(b.Db().PathToGuid("Art/hero.png").IsNull() && !Contains(b.List(p.td.at("Art"), lo), "hero.png"));
        CHECK(b.Undo().ok);
        CHECK(b.Db().PathToGuid("Art/hero.png") == metaGuid);
        badge.clear(); for (auto& e : scenes()) badge[e.name] = e.missingRefs;
        CHECK(badge["main.okayscene"] == 0 && badge["broken.okayscene"] == 1);
    }

    RUN_SUITE("thumbnails: hit, miss, invalidate, persist, lazy within a frame budget");
    {
        Proj p;
        int rendered = 0;
        {
            Browser b(p.td.assets());
            b.AddThumbGenerator({"okayprefab", "okayscene"}, [&](const std::string& path) {
                ++rendered;
                okay::Image im(32, 32); im.Fill(okay::Color(1, 0, 0, 1));
                (void)path;
                return im;
            });
            b.ThumbFrame(1000.0);
            auto t = b.Thumb(p.td.at("Art/hero.png"));
            CHECK(t.ready && !t.placeholder && t.image && t.image->Width() == 64 && !t.key.empty());   // an image: its own picture
            auto st = b.Thumbs().GetStats();
            CHECK(st.generated == 1 && st.memoryHits == 0);
            auto again = b.Thumb(p.td.at("Art/hero.png"));
            CHECK(again.ready && b.Thumbs().GetStats().generated == 1 && b.Thumbs().GetStats().memoryHits == 1);   // hit
            CHECK(b.Thumb(p.td.at("Prefabs/hero.okayprefab")).ready && rendered == 1);                           // a custom generator
            CHECK(b.Thumb(p.td.at("Prefabs/hero.okayprefab")).ready && rendered == 1);
            const auto script = b.Thumb(p.td.at("Scripts/move.okay"));
            CHECK(!script.ready && script.placeholder && !script.failed);                                        // no picture for a script: the icon shows

            okay::AudioClip clip = okay::AudioClip::Const(0.5f, 4000, 8000);
            fs::create_directories(fs::u8path(p.td.at("Audio")));
            CHECK(clip.SaveWAV(p.td.at("Audio/beep.wav")));
            b.Poll(0); b.Sync();
            const auto wav = b.Thumb(p.td.at("Audio/beep.wav"));
            CHECK(wav.ready && wav.image && wav.image->Width() == 128);                                          // a waveform

            // content change: a new key, the old picture is never served
            okay::Guid g1, g2; std::uint64_t h1 = 0, h2 = 0;
            b.ThumbKey(p.td.at("Art/hero.png"), g1, h1);
            WritePng(p.td.at("Art/hero.png"), 64, 64, 77);
            b.Reimport(p.td.at("Art/hero.png"));
            b.ThumbKey(p.td.at("Art/hero.png"), g2, h2);
            CHECK(g1 == g2 && h1 != h2);
            const auto gen = b.Thumbs().GetStats().generated;
            CHECK(b.Thumb(p.td.at("Art/hero.png")).ready && b.Thumbs().GetStats().generated == gen + 1);
            // a prefab's picture also depends on what it uses: its key changes with its texture
            b.ThumbKey(p.td.at("Prefabs/hero.okayprefab"), g1, h1);
            WritePng(p.td.at("Art/hero.png"), 64, 64, 5);
            b.Reimport(p.td.at("Art/hero.png"));
            b.ThumbKey(p.td.at("Prefabs/hero.okayprefab"), g2, h2);
            CHECK(g1 == g2 && h1 != h2);
            // forget
            b.ForgetThumb(p.td.at("Art/rock.png"));
            CHECK(b.Thumb(p.td.at("Art/rock.png")).ready);
            b.ForgetThumb(p.td.at("Art/rock.png"));
            CHECK(!b.Thumbs().InMemory(g1, 1));                           // (an unrelated key is simply absent)
            CHECK(Exists(p.td.path.generic_u8string() + "/.okay/thumbs"));
            CHECK(b.Thumb(p.td.at("Art/hero.png")).ready && b.Thumb(p.td.at("Prefabs/hero.okayprefab")).ready);   // the pictures of the final state are on disk
        }
        {   // a new session (a new Browser): the pictures come from disk, nothing is rendered again
            Browser b(p.td.assets());
            int again = 0;
            b.AddThumbGenerator({"okayprefab"}, [&](const std::string&) { ++again; return okay::Image(); });
            b.ThumbFrame(1000.0);
            CHECK(b.Thumb(p.td.at("Art/hero.png")).ready);
            CHECK(b.Thumb(p.td.at("Prefabs/hero.okayprefab")).ready && again == 0);
            CHECK(b.Thumbs().GetStats().diskHits >= 2 && b.Thumbs().GetStats().generated == 0);
        }
        {   // lazy: a frame budget of 0 makes one picture per frame and leaves the rest for later
            Browser b(p.td.assets());
            for (int i = 0; i < 6; ++i) WritePng(p.td.at("Many/p" + std::to_string(i) + ".png"), 48, 48, 100 + i);
            b.Sync();
            int ready = 0;
            b.ThumbFrame(0.0);
            for (int i = 0; i < 6; ++i) ready += b.Thumb(p.td.at("Many/p" + std::to_string(i) + ".png")).ready ? 1 : 0;
            CHECK(ready == 1);
            for (int frame = 0; frame < 6; ++frame) {
                b.ThumbFrame(0.0);
                for (int i = 0; i < 6; ++i) b.Thumb(p.td.at("Many/p" + std::to_string(i) + ".png"));
            }
            b.ThumbFrame(0.0);
            ready = 0;
            for (int i = 0; i < 6; ++i) ready += b.Thumb(p.td.at("Many/p" + std::to_string(i) + ".png")).ready ? 1 : 0;
            CHECK(ready == 6);                                            // all there after a few frames
            // a deleted asset's picture is pruned from the disk cache
            fs::remove(fs::u8path(p.td.at("Many/p0.png")));
            b.Sync();
            CHECK(b.PruneThumbs() >= 1);
        }
    }

    RUN_SUITE("5,000 assets: refresh cost");
    {
        TempDir td;
        const int kTextures = 3500, kScenes = 1000, kOther = 500;
        auto tc = std::chrono::steady_clock::now();
        for (int i = 0; i < kTextures; ++i) WriteFile(td.at("Textures/set" + std::to_string(i / 100) + "/t" + std::to_string(i) + ".png"), "PNG-" + std::to_string(i));
        for (int i = 0; i < kScenes; ++i) {
            std::string s = "scene \"S" + std::to_string(i) + "\"\n";
            for (int k = 0; k < 4; ++k) s += "gameobject \"o\"\n  sprite texture \"Textures/set" + std::to_string(((i * 4 + k) % kTextures) / 100) + "/t" + std::to_string((i * 4 + k) % kTextures) + ".png\"\n";
            WriteFile(td.at("Scenes/s" + std::to_string(i) + ".okayscene"), s);
        }
        for (int i = 0; i < kOther; ++i) WriteFile(td.at("Other/f" + std::to_string(i) + ".okaydata"), "k=" + std::to_string(i) + "\n");
        const double fixtureMs = Ms(tc);

        Browser b(td.assets());
        auto t0 = std::chrono::steady_clock::now();
        CHECK(b.Sync());
        const double coldMs = Ms(t0);
        CHECK(b.AssetCount() == kTextures + kScenes + kOther);

        std::uint64_t now = 100000000;
        const std::uint64_t rev = b.Revision();
        t0 = std::chrono::steady_clock::now();
        const bool changedIdle = b.Poll(now);
        const double idleMs = Ms(t0);
        CHECK(!changedIdle && b.Revision() == rev);

        WriteFile(td.at("Textures/set0/t0.png"), "PNG-changed-0");
        WriteFile(td.at("Scenes/s0.okayscene"), "scene \"S0\"\ngameobject \"o\"\n  sprite texture \"Textures/set0/t1.png\"\n");
        WriteFile(td.at("Other/new.okaydata"), "x\n");
        fs::remove(fs::u8path(td.at("Other/f0.okaydata")));
        now += 100000000;
        t0 = std::chrono::steady_clock::now();
        const bool changed = b.Poll(now);
        const double editMs = Ms(t0);
        CHECK(changed && b.Revision() > rev && b.AssetCount() == kTextures + kScenes + kOther);

        t0 = std::chrono::steady_clock::now();
        ListOptions o; o.recursive = true; o.text = "t12";
        const auto hits = b.List(td.assets(), o);
        const double searchMs = Ms(t0);
        CHECK(!hits.empty());
        t0 = std::chrono::steady_clock::now();
        const auto all = b.List(td.assets(), ListOptions());
        const double listMs = Ms(t0);
        CHECK(all.size() == 3);
        t0 = std::chrono::steady_clock::now();
        const int users = b.UsersOf(td.at("Textures/set1/t100.png"));
        const double usersMs = Ms(t0);
        CHECK(users >= 1);
        std::printf("  5000-asset refresh: fixture %.0f ms, cold scan %.0f ms, idle poll %.1f ms, poll after 4 edits %.1f ms, search %.1f ms, folder list %.2f ms, used-by %.2f ms\n",
                    fixtureMs, coldMs, idleMs, editMs, searchMs, listMs, usersMs);
        CHECK(idleMs < 400.0);       // budget: an unchanged project re-checks 5,000 assets in well under half a second
        CHECK(editMs < 600.0);       // and picks up a few edits (re-reading only the changed files) within 600 ms
        CHECK(searchMs < 200.0 && listMs < 50.0);
    }

    TEST_MAIN_RESULT();
}
