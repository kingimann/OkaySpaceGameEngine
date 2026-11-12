#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Audio/AudioClip.hpp"
#include "okay/Core/AssetDatabase.hpp"
#include "okay/Core/AssetManager.hpp"
#include "okay/Core/DataPack.hpp"
#include "okay/Core/ImportSettings.hpp"
#include "okay/Core/ThumbnailCache.hpp"
#include "okay/Graphics/Image.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <thread>

using namespace okay;
namespace fs = std::filesystem;

namespace {

std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
void WriteText(const fs::path& p, const std::string& s) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}
void WritePng(const fs::path& p, int w, int h, Color c) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    Image img(w, h);
    img.Fill(c);
    img.SavePNG(p.string());
}
// Make a rewrite visible even where the clock is coarse.
void Bump(const fs::path& p, int seconds) {
    fs::last_write_time(p, fs::last_write_time(p) + std::chrono::seconds(seconds));
}
void SetAge(const fs::path& p, int hoursAgo) {
    fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::hours(hoursAgo));
}
bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
bool Contains(const std::vector<Guid>& v, const Guid& g) { return std::find(v.begin(), v.end(), g) != v.end(); }

struct Project {
    fs::path dir;
    explicit Project(const std::string& tag) {
        dir = fs::temp_directory_path() / ("okay_adb_" + tag + "_" + std::to_string(std::random_device{}()));
        fs::create_directories(dir);
    }
    ~Project() { std::error_code ec; fs::remove_all(dir, ec); }
    fs::path P(const std::string& rel) const { return dir / rel; }
    std::string Root() const { return dir.generic_string(); }
    void Text(const std::string& rel, const std::string& s) { WriteText(P(rel), s); }
    void Png(const std::string& rel, Color c, int w = 8, int h = 8) { WritePng(P(rel), w, h, c); }
    std::string Read(const std::string& rel) const { return ReadText(P(rel)); }
    bool Exists(const std::string& rel) const { std::error_code ec; return fs::exists(P(rel), ec); }
    void Move(const std::string& from, const std::string& to, bool withMeta = true) {
        std::error_code ec;
        fs::create_directories(P(to).parent_path(), ec);
        fs::rename(P(from), P(to), ec);
        if (withMeta) fs::rename(P(from + ".okaymeta"), P(to + ".okaymeta"), ec);
    }
};

// A small fake game: a scene, a prefab, a material, dialogue, a behavior tree, a cutscene, a cycle.
void BuildGame(Project& p) {
    p.Png("art/hero.png", Color(1, 0, 0, 1));
    p.Png("art/enemy.png", Color(0, 1, 0, 1));
    p.Png("art/stone.png", Color(0, 0, 1, 1));
    p.Png("art/unused.png", Color(1, 1, 0, 1));
    p.Png("art/npc.png", Color(1, 0, 1, 1));
    p.Png("art/bt.png", Color(0, 1, 1, 1));
    p.Png("art/win.png", Color(0.5f, 0.5f, 0.5f, 1));
    p.Png("prefabs/enemy_tex.png", Color(0.2f, 0.4f, 0.6f, 1));
    p.Text("audio/boom.wav", "RIFF-boom");
    p.Text("audio/hi.wav", "RIFF-hi");
    p.Text("audio/alarm.wav", "RIFF-alarm");
    p.Text("audio/cue.wav", "RIFF-cue");
    p.Text("audio/unused.wav", "RIFF-unused");
    p.Text("scenes/main.okayscene",
        "okayscene 1\n"
        "name \"Level1\"\n"
        "gameobject 0 \"decoy.png\"\n"
        "  active 1\n"
        "  parent -1\n"
        "  tag \"tagged.png\"\n"
        "  sprite 0 0 \"art/hero.png\" 1 1 1 1\n"
        "  audio \"audio/boom.wav\" 1 1 0 1\n"
        "  prefab \"prefabs/enemy.okayprefab\"\n"
        "  spriterenderer \"art/missing.png\" 1\n"
        "  uitext \"see hero art.png\" 12\n"
        "  visualscriptpath \"scripts/none.okayvs\"\n"
        "  netprefab \"https://example.com/remote.png\"\n"
        "end\n");
    p.Text("scenes/win.okayscene",
        "okayscene 1\n"
        "gameobject 0 \"Win\"\n"
        "  uiimage \"art\\\\win.png\" 1\n"
        "end\n");
    p.Text("prefabs/enemy.okayprefab",
        "okayscene 1\n"
        "gameobject 0 \"Enemy\"\n"
        "  parent -1\n"
        "  sprite \"enemy_tex.png\" 1\n"
        "  meshrenderer \"art/enemy.png\" \"materials/stone.okaymat\"\n"
        "end\n");
    p.Text("prefabs/cyc_a.okayprefab", "okayscene 1\ngameobject 0 \"A\"\n  prefab \"prefabs/cyc_b.okayprefab\"\nend\n");
    p.Text("prefabs/cyc_b.okayprefab", "okayscene 1\ngameobject 0 \"B\"\n  prefab \"prefabs/cyc_a.okayprefab\"\nend\n");
    p.Text("materials/stone.okaymat", "okaymat 1\ncolor 1 1 1 1\ntexture art/stone.png\n");
    p.Text("dialogue/intro.okaydialog",
        "okaydialog 1\nname \"intro\"\nstart 0\n"
        "node id=0 kind=\"line\" x=0 y=0 speaker=\"Bob\" text=\"Hello there.\" portrait=\"art/npc.png\" sound=\"audio/hi.wav\" next=-1\n");
    p.Text("ai/guard.okaybt",
        "okaybt 1\nname Guard\n"
        "node 0 Sequence parent=-1 x=0 y=0 name=Guard\n"
        "node 1 PlaySound parent=0 x=0 y=0 clip=audio/alarm.wav icon=\"art/bt.png\"\n");
    p.Text("cutscenes/intro.okayseq", "okayseq 1\naudio \"Cam\"\n  cue 0.5 \"audio/cue.wav\" 1\n");
    p.Text("readme.txt", "just a note");
}

constexpr int kAssets = 23;   // the files BuildGame makes that count as assets

} // namespace

int main() {
    RUN_SUITE("asset_database");

    // --- Guids ---------------------------------------------------------------------------------
    {
        std::set<std::string> seen;
        for (int i = 0; i < 5000; ++i) seen.insert(Guid::Generate().ToString());
        CHECK(seen.size() == 5000);
        const Guid g = Guid::Generate();
        CHECK(!g.IsNull());
        CHECK(g.ToString().size() == 32);
        Guid back;
        CHECK(Guid::Parse(g.ToString(), back) && back == g);
        CHECK(Guid::Parse("{" + g.ToString().substr(0, 8) + "-" + g.ToString().substr(8, 4) + "-" + g.ToString().substr(12, 4) + "-" +
                          g.ToString().substr(16, 4) + "-" + g.ToString().substr(20) + "}", back) && back == g);
        std::string upper = g.ToString();
        for (char& c : upper) if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 32);
        CHECK(Guid::Parse(upper, back) && back == g);
        CHECK(!Guid::Parse("", back));
        CHECK(!Guid::Parse("xyz", back));
        CHECK(!Guid::Parse(std::string(32, 'g'), back));
        CHECK(!Guid::Parse(g.ToString() + "0", back));
        CHECK(Guid().IsNull());
    }

    // --- Kinds and extensions --------------------------------------------------------------------
    {
        CHECK(AssetTypeFromExtension("PNG") == AssetType::Texture);
        CHECK(AssetTypeFromExtension(".glb") == AssetType::Model);
        CHECK(AssetTypeFromExtension("okayscene") == AssetType::Scene);
        CHECK(AssetTypeFromExtension("zzz") == AssetType::Other);
        CHECK(AssetTypeFromPath("a/b/Hero.JPG") == AssetType::Texture);
        CHECK(AssetExtension("a.b/c") .empty());
        CHECK(AssetExtension("dir/file.tar.WAV") == "wav");
        CHECK(AssetFileName("a\\b/c.png") == "c.png");
        CHECK(AssetTypeFromName("Texture") == AssetType::Texture);
        CHECK(AssetTypeFromName("nonsense") == AssetType::Unknown);
        CHECK(std::string(AssetTypeName(AssetType::BehaviorTree)) == "behaviortree");
        const auto texExt = ExtensionsOfType(AssetType::Texture);
        CHECK(std::find(texExt.begin(), texExt.end(), "png") != texExt.end());
    }

    // --- Import settings: defaults, round trip, clamping ----------------------------------------------
    {
        ImportSettings t = ImportSettings::Defaults(AssetType::Texture);
        CHECK(t.version == ImportSettings::CurrentVersion(AssetType::Texture));
        CHECK(t.texture.sRGB && t.texture.mipmaps && !t.texture.compress && t.texture.maxSize == 2048);
        t.texture.sRGB = false;
        t.texture.filter = TextureImportSettings::Filter::Point;
        t.texture.wrap = TextureImportSettings::Wrap::Clamp;
        t.texture.maxSize = 512;
        t.texture.compress = true;
        ImportSettings back = ImportSettings::FromPairs(AssetType::Texture, t.version, t.ToPairs());
        CHECK(back.SameAs(t));
        CHECK(!back.texture.sRGB && back.texture.filter == TextureImportSettings::Filter::Point && back.texture.maxSize == 512);

        ImportSettings m = ImportSettings::Defaults(AssetType::Model);
        m.model.scale = 0.01f;
        m.model.normals = ModelImportSettings::Normals::Flat;
        m.model.importAnimations = false;
        ImportSettings mb = ImportSettings::FromPairs(AssetType::Model, m.version, m.ToPairs());
        CHECK(mb.SameAs(m));
        CHECK_NEAR(mb.model.scale, 0.01f, 1e-9);

        ImportSettings a = ImportSettings::Defaults(AssetType::Audio);
        a.audio.loadMode = AudioImportSettings::LoadMode::Stream;
        a.audio.forceMono = true;
        a.audio.quality = 0.35f;
        ImportSettings ab = ImportSettings::FromPairs(AssetType::Audio, a.version, a.ToPairs());
        CHECK(ab.SameAs(a));

        // Bad values take defaults and say so; ranges clamp.
        std::vector<std::string> notes;
        ImportSettings bad = ImportSettings::FromPairs(AssetType::Texture, 2,
            {{"filter", "fancy"}, {"maxSize", "999999"}, {"srgb", "maybe"}, {"wrap", "CLAMP"}}, &notes);
        CHECK(bad.texture.filter == TextureImportSettings::Filter::Trilinear);
        CHECK(bad.texture.maxSize == 16384);
        CHECK(bad.texture.sRGB);
        CHECK(bad.texture.wrap == TextureImportSettings::Wrap::Clamp);
        CHECK(notes.size() == 2);
        ImportSettings tiny = ImportSettings::FromPairs(AssetType::Texture, 2, {{"maxSize", "3"}});
        CHECK(tiny.texture.maxSize == 32);
        ImportSettings unlimited = ImportSettings::FromPairs(AssetType::Texture, 2, {{"maxSize", "0"}});
        CHECK(unlimited.texture.maxSize == 0);
        ImportSettings sc = ImportSettings::FromPairs(AssetType::Model, 1, {{"scale", "-5"}, {"smoothingAngle", "400"}});
        CHECK(sc.model.scale > 0.0f && sc.model.smoothingAngle == 180.0f);

        // Kinds without importer options keep nothing but extras.
        CHECK(!ImportSettings::HasSettings(AssetType::Scene));
        CHECK(ImportSettings::Defaults(AssetType::Scene).ToPairs().empty());
    }

    // --- Import settings: migration -------------------------------------------------------------------------
    {
        std::vector<std::string> notes;
        ImportSettings t = ImportSettings::FromPairs(AssetType::Texture, 1,
            {{"linear", "1"}, {"filter", "nearest"}, {"maxsize", "512"}, {"mips", "0"}, {"wrap", "clamp"}}, &notes);
        CHECK(t.version == 2);
        CHECK(!t.texture.sRGB);                       // linear -> srgb 0
        CHECK(t.texture.filter == TextureImportSettings::Filter::Point);
        CHECK(t.texture.maxSize == 512);
        CHECK(!t.texture.mipmaps);
        CHECK(t.texture.wrap == TextureImportSettings::Wrap::Clamp);
        CHECK(!t.texture.compress);                   // new in v2: default
        CHECK(!notes.empty());
        ImportSettings t2 = ImportSettings::FromPairs(AssetType::Texture, 1, {{"linear", "0"}, {"filter", "linear"}});
        CHECK(t2.texture.sRGB && t2.texture.filter == TextureImportSettings::Filter::Bilinear);

        ImportSettings a = ImportSettings::FromPairs(AssetType::Audio, 1, {{"stream", "1"}, {"mono", "1"}, {"quality", "40"}});
        CHECK(a.audio.loadMode == AudioImportSettings::LoadMode::Stream);
        CHECK(a.audio.forceMono);
        CHECK_NEAR(a.audio.quality, 0.4f, 1e-6);

        // A newer layout: read what we know, keep the rest, flag it.
        ImportSettings n = ImportSettings::FromPairs(AssetType::Texture, 9, {{"srgb", "0"}, {"futureThing", "yes"}});
        CHECK(n.fromNewerVersion);
        CHECK(!n.texture.sRGB);
        CHECK(n.extra.count("futureThing") == 1);
        bool hasFuture = false;
        for (const auto& kv : n.ToPairs()) if (kv.first == "futureThing" && kv.second == "yes") hasFuture = true;
        CHECK(hasFuture);
    }

    // --- Scan: creates metas, never touches assets ---------------------------------------------------------
    {
        Project p("scan");
        BuildGame(p);
        p.Text(".hidden/secret.png", "x");
        p.Text("art/hero.png.tmp", "x");
        const std::string heroBefore = p.Read("art/hero.png");
        const auto heroTime = fs::last_write_time(p.P("art/hero.png"));

        AssetDatabase db;
        ScanReport r = db.Scan(p.Root());
        CHECK(r.scanned == kAssets);
        CHECK(r.created == kAssets);
        CHECK(db.Count() == static_cast<std::size_t>(kAssets));
        CHECK(r.moved.empty() && r.duplicates.empty() && r.orphanMetas.empty() && r.removed.empty());
        CHECK(p.Exists("art/hero.png.okaymeta"));
        CHECK(p.Exists("scenes/main.okayscene.okaymeta"));
        CHECK(!p.Exists(".hidden/secret.png.okaymeta"));
        CHECK(!p.Exists("art/hero.png.tmp.okaymeta"));
        CHECK(!p.Exists("art/hero.png.okaymeta.okaymeta"));
        CHECK(p.Read("art/hero.png") == heroBefore);
        CHECK(fs::last_write_time(p.P("art/hero.png")) == heroTime);

        const AssetRecord* hero = db.Find("art/hero.png");
        CHECK(hero != nullptr);
        if (hero) {
            CHECK(hero->kind == AssetType::Texture);
            CHECK(hero->size > 0 && hero->hash != 0);
            CHECK(hero->settings.kind == AssetType::Texture);
            CHECK(hero->metaVersion == AssetDatabase::kMetaVersion);
            const std::string meta = p.Read("art/hero.png.okaymeta");
            CHECK(meta.rfind("okaymeta 1\n", 0) == 0);
            CHECK(Has(meta, "guid " + hero->guid.ToString()));
            CHECK(Has(meta, "type texture"));
            CHECK(Has(meta, "importer texture 2"));
            CHECK(Has(meta, "set srgb 1"));

            // Lookups.
            CHECK(db.GuidToPath(hero->guid) == "art/hero.png");
            CHECK(db.PathToGuid("art/hero.png") == hero->guid);
            CHECK(db.PathToGuid("./art/../art/hero.png") == hero->guid);
            CHECK(db.PathToGuid("art\\hero.png") == hero->guid);
            CHECK(db.PathToGuid(p.Root() + "/art/hero.png") == hero->guid);
            CHECK(db.PathToGuid("art/nothing.png").IsNull());
            CHECK(db.PathToGuid("../elsewhere/hero.png").IsNull());
            CHECK(db.Resolve(hero->guid.ToString()).path == "art/hero.png");
            CHECK(db.Resolve("art/hero.png").guid == hero->guid);
            CHECK(!db.Resolve("art/hero.png").viaRedirect);
            CHECK(!db.Resolve("nope.png"));
            CHECK(!db.Resolve(Guid::Generate().ToString()));
            CHECK(db.AbsolutePath(hero->guid) == db.Root() + "/art/hero.png");
            CHECK(db.ToProjectPath(p.Root() + "/art/hero.png") == "art/hero.png");
            CHECK(db.ToProjectPath("/somewhere/else.png").empty());
        }
        CHECK(db.Find("readme.txt") != nullptr);
        CHECK(db.Find("readme.txt")->kind == AssetType::Data);
        CHECK(db.Find("scenes/main.okayscene")->kind == AssetType::Scene);
        CHECK(db.Find("audio/boom.wav")->kind == AssetType::Audio);
        CHECK(db.Find("audio/boom.wav")->settings.kind == AssetType::Audio);
        CHECK(!ImportSettings::HasSettings(db.Find("scenes/main.okayscene")->kind));

        // A second scan changes nothing: same guids, no rewrites, no events.
        std::map<std::string, Guid> before;
        for (const AssetRecord* a : db.All()) before[a->path] = a->guid;
        const std::string metaBefore = p.Read("art/hero.png.okaymeta");
        const auto metaTime = fs::last_write_time(p.P("art/hero.png.okaymeta"));
        ScanReport r2 = db.Scan(p.Root());
        CHECK(r2.created == 0 && r2.moved.empty() && r2.reimports.empty() && r2.upgraded == 0);
        CHECK(db.Count() == static_cast<std::size_t>(kAssets));
        for (const AssetRecord* a : db.All()) CHECK(before[a->path] == a->guid);
        CHECK(p.Read("art/hero.png.okaymeta") == metaBefore);
        CHECK(fs::last_write_time(p.P("art/hero.png.okaymeta")) == metaTime);

        // A brand new database on the same project reads the same guids back (persistence).
        AssetDatabase db2;
        ScanReport r3 = db2.Scan(p.Root());
        CHECK(r3.created == 0 && r3.reimports.empty());
        for (const AssetRecord* a : db2.All()) CHECK(before[a->path] == a->guid);

        // createMetas off: nothing is written for new files.
        p.Png("art/late.png", Color(0.9f, 0.1f, 0.1f, 1));
        ScanOptions off;
        off.createMetas = false;
        ScanReport r4 = db2.Scan(p.Root(), off);
        CHECK(r4.unmetaed == 1);
        CHECK(!p.Exists("art/late.png.okaymeta"));
        CHECK(db2.Find("art/late.png") == nullptr);
    }

    // --- Moves and renames --------------------------------------------------------------------------------
    {
        Project p("moves");
        BuildGame(p);
        AssetDatabase db;
        db.Scan(p.Root());
        const Guid hero = db.PathToGuid("art/hero.png");
        CHECK(db.AddLabel(hero, "player"));

        // (a) The asset and its meta move together (a sync tool, git mv, the editor).
        p.Move("art/hero.png", "art/characters/hero_idle.png");
        ScanReport r = db.Scan(p.Root());
        CHECK(r.moved.size() == 1 && r.created == 0);
        if (!r.moved.empty()) {
            CHECK(r.moved[0].guid == hero);
            CHECK(r.moved[0].from == "art/hero.png" && r.moved[0].to == "art/characters/hero_idle.png");
            CHECK(r.moved[0].how == "guid");
        }
        CHECK(db.GuidToPath(hero) == "art/characters/hero_idle.png");
        CHECK(db.PathToGuid("art/characters/hero_idle.png") == hero);
        CHECK(db.PathToGuid("art/hero.png").IsNull());
        CHECK(db.Resolve("art/hero.png").guid == hero);          // the old path still resolves
        CHECK(db.Resolve("art/hero.png").viaRedirect);
        CHECK(db.Resolve("art/hero.png").path == "art/characters/hero_idle.png");
        CHECK(db.Find(hero)->HasLabel("player"));
        CHECK(r.removed.empty());

        // (b) Only the asset moves; its meta is left behind (an OS file manager): matched by content.
        p.Move("art/enemy.png", "art/mobs/enemy.png", /*withMeta=*/false);
        const Guid enemy = db.PathToGuid("art/enemy.png");
        CHECK(p.Exists("art/enemy.png.okaymeta"));
        ScanReport rb = db.Scan(p.Root());
        CHECK(rb.moved.size() == 1);
        if (!rb.moved.empty()) CHECK(rb.moved[0].how == "hash" && rb.moved[0].guid == enemy);
        CHECK(db.GuidToPath(enemy) == "art/mobs/enemy.png");
        CHECK(p.Exists("art/mobs/enemy.png.okaymeta"));
        CHECK(!p.Exists("art/enemy.png.okaymeta"));          // the meta followed the asset
        CHECK(rb.orphanMetas.empty() && rb.created == 0);

        // ... and across sessions: a fresh database finds the stale meta by the hash stored in it.
        p.Move("art/stone.png", "art/rock/stone2.png", /*withMeta=*/false);
        AssetDatabase fresh;
        const std::string stoneMeta = p.Read("art/stone.png.okaymeta");
        Guid stoneGuid;
        {
            const std::size_t at = stoneMeta.find("guid ") + 5;
            Guid::Parse(stoneMeta.substr(at, 32), stoneGuid);
        }
        ScanReport rf = fresh.Scan(p.Root());
        CHECK(rf.moved.size() == 1);
        if (!rf.moved.empty()) CHECK(rf.moved[0].how == "hash" && rf.moved[0].guid == stoneGuid);
        CHECK(fresh.PathToGuid("art/rock/stone2.png") == stoneGuid);
        CHECK(rf.orphanMetas.empty());

        // (c) The meta is lost along with the move: the database still remembers the content.
        const Guid npc = db.PathToGuid("art/npc.png");
        CHECK(db.SetLabels(npc, {"npc", "Face"}));
        fs::remove(p.P("art/npc.png.okaymeta"));
        p.Move("art/npc.png", "art/faces/npc_face.png", /*withMeta=*/false);
        ScanReport rc = db.Scan(p.Root());
        CHECK(rc.moved.size() >= 1);
        bool npcMoved = false;
        for (const auto& m : rc.moved) if (m.guid == npc && m.to == "art/faces/npc_face.png") npcMoved = true;
        CHECK(npcMoved);
        CHECK(db.GuidToPath(npc) == "art/faces/npc_face.png");
        CHECK(db.Find(npc)->HasLabel("npc") && db.Find(npc)->HasLabel("face"));   // labels came back from memory
        CHECK(Has(p.Read("art/faces/npc_face.png.okaymeta"), "guid " + npc.ToString()));

        // (d) The meta alone is deleted; the asset stays: the guid is restored, not re-rolled.
        const Guid bt = db.PathToGuid("art/bt.png");
        fs::remove(p.P("art/bt.png.okaymeta"));
        ScanReport rd = db.Scan(p.Root());
        CHECK(rd.created == 0 && rd.moved.empty());
        CHECK(db.PathToGuid("art/bt.png") == bt);
        CHECK(Has(p.Read("art/bt.png.okaymeta"), "guid " + bt.ToString()));

        // (e) Renamed AND edited, meta lost: the last resort matches by name when it is unambiguous.
        const Guid win = db.PathToGuid("art/win.png");
        fs::remove(p.P("art/win.png"));
        db.Scan(p.Root());   // the asset is gone, its meta is an orphan
        CHECK(db.Find(win) == nullptr);
        CHECK(!db.OrphanGuids().empty());
        p.Png("art/new_home/win.png", Color(0.1f, 0.2f, 0.3f, 1), 8, 8);   // same name, different content
        ScanReport re = db.Scan(p.Root());
        bool byName = false;
        for (const auto& m : re.moved) if (m.guid == win && m.how == "name") byName = true;
        CHECK(byName);
        CHECK(db.PathToGuid("art/new_home/win.png") == win);
        CHECK(!re.warnings.empty());
        CHECK(db.OrphanGuids().empty());
    }

    // --- Deleted assets and orphan metas --------------------------------------------------------------------
    {
        Project p("orphans");
        BuildGame(p);
        AssetDatabase db;
        db.Scan(p.Root());
        const Guid gone = db.PathToGuid("art/unused.png");
        fs::remove(p.P("art/unused.png"));              // the meta stays: an orphan
        ScanReport r = db.Scan(p.Root());
        CHECK(r.removed.size() == 1 && r.removed[0] == "art/unused.png");
        CHECK(r.orphanMetas.size() == 1 && r.orphanMetas[0] == "art/unused.png");
        CHECK(db.Find(gone) == nullptr);
        CHECK(db.OrphanGuids().size() == 1 && db.OrphanGuids()[0] == gone);
        CHECK(p.Exists("art/unused.png.okaymeta"));      // never deleted behind the user's back
        CHECK(db.RemoveOrphanMetas() == 1);
        CHECK(!p.Exists("art/unused.png.okaymeta"));

        // deleteOrphanMetas in the scan options does the same.
        fs::remove(p.P("audio/unused.wav"));
        ScanOptions o;
        o.deleteOrphanMetas = true;
        ScanReport r2 = db.Scan(p.Root(), o);
        CHECK(r2.orphanMetas.size() == 1);
        CHECK(!p.Exists("audio/unused.wav.okaymeta"));
        CHECK(db.OrphanGuids().empty());

        // The deleted asset comes back at the same path with the meta restored from an older copy.
        // (An asset without its meta that reappears later is matched by content from memory.)
        const Guid boom = db.PathToGuid("audio/boom.wav");
        const std::string saved = p.Read("audio/boom.wav");
        fs::remove(p.P("audio/boom.wav"));
        fs::remove(p.P("audio/boom.wav.okaymeta"));
        db.Scan(p.Root());
        CHECK(db.Find(boom) == nullptr);
        p.Text("audio/sounds/boom_renamed.wav", saved);
        ScanReport r3 = db.Scan(p.Root());
        CHECK(db.PathToGuid("audio/sounds/boom_renamed.wav") == boom);
        CHECK(r3.moved.size() == 1 && r3.moved[0].how == "hash");
    }

    // --- Duplicate guids (copied metas) ------------------------------------------------------------------------
    {
        Project p("dups");
        BuildGame(p);
        AssetDatabase db;
        db.Scan(p.Root());
        const Guid orig = db.PathToGuid("art/hero.png");
        CHECK(db.AddLabel(orig, "shared"));
        ImportSettings s = *db.GetSettings(orig);
        s.texture.filter = TextureImportSettings::Filter::Point;
        CHECK(db.SetSettings(orig, s));
        db.TakeReimports();

        // Copy the asset and its meta: both claim the same guid.
        fs::copy_file(p.P("art/hero.png"), p.P("art/hero_copy.png"));
        fs::copy_file(p.P("art/hero.png.okaymeta"), p.P("art/hero_copy.png.okaymeta"));
        SetAge(p.P("art/hero.png.okaymeta"), 5);          // the original's meta is older
        ScanReport r = db.Scan(p.Root());
        CHECK(r.duplicates.size() == 1);
        if (!r.duplicates.empty()) {
            CHECK(r.duplicates[0].path == "art/hero_copy.png");
            CHECK(r.duplicates[0].original == orig);
            CHECK(r.duplicates[0].replacement != orig);
        }
        CHECK(!r.warnings.empty());
        CHECK(db.PathToGuid("art/hero.png") == orig);     // the original keeps it
        const Guid copy = db.PathToGuid("art/hero_copy.png");
        CHECK(!copy.IsNull() && copy != orig);
        CHECK(Has(p.Read("art/hero_copy.png.okaymeta"), "guid " + copy.ToString()));
        CHECK(Has(p.Read("art/hero.png.okaymeta"), "guid " + orig.ToString()));
        CHECK(db.Find(copy)->HasLabel("shared"));          // the copy brings its labels and settings along
        CHECK(db.GetSettings(copy)->texture.filter == TextureImportSettings::Filter::Point);
        CHECK(r.created == 1);

        // A fresh database (no memory of who was first) picks the older meta as the original.
        Project q("dups2");
        q.Png("a.png", Color(1, 0, 0, 1));
        AssetDatabase d1;
        d1.Scan(q.Root());
        const Guid ga = d1.PathToGuid("a.png");
        fs::copy_file(q.P("a.png"), q.P("b.png"));
        fs::copy_file(q.P("a.png.okaymeta"), q.P("b.png.okaymeta"));
        SetAge(q.P("a.png.okaymeta"), 3);
        AssetDatabase d2;
        ScanReport r2 = d2.Scan(q.Root());
        CHECK(r2.duplicates.size() == 1 && r2.duplicates[0].path == "b.png");
        CHECK(d2.PathToGuid("a.png") == ga && d2.PathToGuid("b.png") != ga);

        // Injected GUID source (deterministic ids for tests).
        Project z("dups3");
        z.Png("x.png", Color(0, 0, 0, 1));
        AssetDatabase d3;
        std::uint64_t n = 0;
        d3.SetGuidGenerator([&n] { Guid g; g.lo = ++n; g.hi = 7; return g; });
        d3.Scan(z.Root());
        CHECK(d3.PathToGuid("x.png").hi == 7 && d3.PathToGuid("x.png").lo == 1);
    }

    // --- Corrupt and newer metas -------------------------------------------------------------------------------
    {
        Project p("badmeta");
        p.Png("a.png", Color(1, 0, 0, 1));
        p.Png("b.png", Color(0, 1, 0, 1));
        p.Text("a.png.okaymeta", "this is not a meta file\n");
        // A newer meta layout: read, never rewritten.
        p.Text("b.png.okaymeta",
               "okaymeta 7\nguid 0123456789abcdef0123456789abcdef\ntype texture\nsize 1\nimporter texture 2\nset srgb 0\n"
               "futurefield 42\n");
        const std::string newerBefore = p.Read("b.png.okaymeta");
        AssetDatabase db;
        ScanReport r = db.Scan(p.Root());
        CHECK(p.Exists("a.png.okaymeta.corrupt"));
        CHECK(Has(p.Read("a.png.okaymeta.corrupt"), "not a meta file"));
        CHECK(Has(p.Read("a.png.okaymeta"), "okaymeta 1"));
        CHECK(!db.PathToGuid("a.png").IsNull());
        CHECK(r.warnings.size() >= 2);
        const Guid b = db.PathToGuid("b.png");
        CHECK(b.ToString() == "0123456789abcdef0123456789abcdef");
        CHECK(db.Find(b)->metaReadOnly);
        CHECK(!db.GetSettings(b)->texture.sRGB);
        CHECK(p.Read("b.png.okaymeta") == newerBefore);          // untouched, even though the hash drifted
        std::string err;
        CHECK(!db.AddLabel(b, "x", &err));
        CHECK(!err.empty());
        ImportSettings ns = *db.GetSettings(b);
        ns.texture.sRGB = true;
        CHECK(!db.SetSettings(b, ns, &err));
        CHECK(p.Read("b.png.okaymeta") == newerBefore);
        // The .corrupt file is not an asset.
        CHECK(db.Find("a.png.okaymeta.corrupt") == nullptr);
        ScanReport r2 = db.Scan(p.Root());
        CHECK(r2.created == 0);
    }

    // --- Import settings in metas: edits, migration, reimport signal ---------------------------------------------------
    {
        Project p("settings");
        p.Png("tex/a.png", Color(1, 0, 0, 1));
        p.Text("audio/s.wav", "RIFF-s");
        p.Text("scenes/s.okayscene", "okayscene 1\n");
        AssetDatabase db;
        std::vector<ReimportEvent> seen;
        db.SetReimportCallback([&](const ReimportEvent& e) { seen.push_back(e); });
        ScanReport r = db.Scan(p.Root());
        CHECK(r.reimports.size() == 3);                        // three new assets
        for (const auto& e : r.reimports) CHECK(e.reason == ReimportEvent::Reason::Added);
        CHECK(seen.size() == 3);
        CHECK(db.TakeReimports().size() == 3);
        CHECK(db.TakeReimports().empty());
        seen.clear();

        const Guid a = db.PathToGuid("tex/a.png");
        // Same values: nothing happens.
        CHECK(db.SetSettings(a, *db.GetSettings(a)));
        CHECK(seen.empty());
        // A change is stored and signalled once.
        ImportSettings s = *db.GetSettings(a);
        s.texture.maxSize = 256;
        s.texture.mipmaps = false;
        s.texture.compress = true;
        CHECK(db.SetSettings(a, s));
        CHECK(seen.size() == 1 && seen[0].reason == ReimportEvent::Reason::SettingsChanged && seen[0].guid == a);
        CHECK(Has(p.Read("tex/a.png.okaymeta"), "set maxSize 256"));
        CHECK(Has(p.Read("tex/a.png.okaymeta"), "set compress 1"));
        CHECK(db.GetSettings(a)->texture.maxSize == 256);
        // Values are clamped on the way in.
        s.texture.maxSize = 100000;
        CHECK(db.SetSettings(a, s));
        CHECK(db.GetSettings(a)->texture.maxSize == 16384);
        // The wrong kind, an unknown asset, and types without settings are refused.
        std::string err;
        CHECK(!db.SetSettings(a, ImportSettings::Defaults(AssetType::Audio), &err) && !err.empty());
        CHECK(!db.SetSettings(Guid::Generate(), s, &err));
        CHECK(!db.SetSettings(db.PathToGuid("scenes/s.okayscene"), ImportSettings::Defaults(AssetType::Scene), &err));
        seen.clear();

        // A settings edit made on disk (a merge, a text editor) is noticed by the next scan.
        {
            std::string meta = p.Read("audio/s.wav.okaymeta");
            const std::size_t at = meta.find("set loadMode decode");
            CHECK(at != std::string::npos);
            meta.replace(at, std::string("set loadMode decode").size(), "set loadMode stream");
            WriteText(p.P("audio/s.wav.okaymeta"), meta);
        }
        ScanReport r2 = db.Scan(p.Root());
        CHECK(db.GetSettings(db.PathToGuid("audio/s.wav"))->audio.loadMode == AudioImportSettings::LoadMode::Stream);
        CHECK(r2.reimports.size() == 1 && r2.reimports[0].reason == ReimportEvent::Reason::SettingsChanged);
        CHECK(seen.size() == 1);

        // The source changed: the hash moves and a signal fires (during Scan, and via RefreshFile).
        seen.clear();
        const std::uint64_t oldHash = db.Find(a)->hash;
        p.Png("tex/a.png", Color(0, 0, 1, 1));
        Bump(p.P("tex/a.png"), 5);
        ScanReport r3 = db.Scan(p.Root());
        CHECK(r3.reimports.size() == 1 && r3.reimports[0].reason == ReimportEvent::Reason::SourceChanged);
        CHECK(db.Find(a)->hash != oldHash);
        CHECK(Has(p.Read("tex/a.png.okaymeta"), "hash " + savefmt::Hex16(db.Find(a)->hash)));

        // RefreshFile: the hot reload path. A touched file signals once; an untouched one does not.
        seen.clear();
        p.Png("tex/a.png", Color(1, 0, 0, 1));
        Bump(p.P("tex/a.png"), 10);
        CHECK(db.RefreshFile("tex/a.png") == a);
        CHECK(seen.size() == 1 && seen[0].reason == ReimportEvent::Reason::SourceChanged);
        CHECK(db.RefreshFile("tex/a.png") == a);
        CHECK(seen.size() == 1);
        // A file the database has not seen yet is indexed.
        p.Png("tex/b.png", Color(0, 1, 0, 1));
        const Guid bGuid = db.RefreshFile("tex/b.png");
        CHECK(!bGuid.IsNull() && db.GuidToPath(bGuid) == "tex/b.png" && p.Exists("tex/b.png.okaymeta"));
        CHECK(seen.back().reason == ReimportEvent::Reason::Added);
        CHECK(db.RefreshFile("tex/none.png").IsNull());
        CHECK(db.RefreshFile("/elsewhere/x.png").IsNull());
    }

    // --- Settings on disk from an older layout are migrated and the meta upgraded ---------------------------------
    {
        Project p("migrate");
        p.Png("tex/old.png", Color(1, 1, 1, 1));
        p.Text("tex/old.png.okaymeta",
               "okaymeta 1\nguid 11111111111111111111111111111111\ntype texture\nsize 5\n"
               "label old\nimporter texture 1\nset linear 1\nset filter nearest\nset maxsize 512\nset mips 0\n");
        p.Png("tex/keep.png", Color(0, 0, 0, 1));
        p.Text("tex/keep.png.okaymeta",
               "okaymeta 1\nguid 22222222222222222222222222222222\ntype texture\nsize 5\nimporter texture 1\nset linear 1\n");
        p.Text("audio/old.wav", "RIFF-old");
        p.Text("audio/old.wav.okaymeta",
               "okaymeta 1\nguid 33333333333333333333333333333333\ntype audio\nsize 8\nimporter audio 1\nset stream 1\nset mono 1\nset quality 25\n");
        AssetDatabase db;
        ScanOptions noUpgrade;
        noUpgrade.upgradeMetas = false;
        // First without rewriting: the values are migrated in memory, the file is left alone.
        const std::string keepBefore = p.Read("tex/keep.png.okaymeta");
        ScanReport r0 = db.Scan(p.Root(), noUpgrade);
        CHECK(r0.upgraded == 0);
        CHECK(!db.GetSettings(db.PathToGuid("tex/keep.png"))->texture.sRGB);
        CHECK(p.Read("tex/keep.png.okaymeta") == keepBefore);      // old metas are left exactly as they are
        CHECK(Has(p.Read("tex/old.png.okaymeta"), "importer texture 1"));

        AssetDatabase db2;
        ScanReport r = db2.Scan(p.Root());
        CHECK(r.upgraded == 3);
        const Guid oldTex = db2.PathToGuid("tex/old.png");
        CHECK(oldTex.ToString() == "11111111111111111111111111111111");
        const ImportSettings* t = db2.GetSettings(oldTex);
        CHECK(t && t->version == 2 && !t->texture.sRGB && t->texture.filter == TextureImportSettings::Filter::Point &&
              t->texture.maxSize == 512 && !t->texture.mipmaps);
        const std::string meta = p.Read("tex/old.png.okaymeta");
        CHECK(Has(meta, "importer texture 2"));
        CHECK(Has(meta, "set srgb 0"));
        CHECK(Has(meta, "set filter point"));
        CHECK(Has(meta, "set mipmaps 0"));
        CHECK(Has(meta, "set maxSize 512"));
        CHECK(Has(meta, "set compress 0"));
        CHECK(!Has(meta, "set linear") && !Has(meta, "set mips"));
        CHECK(Has(meta, "label old"));
        CHECK(db2.Find(oldTex)->HasLabel("old"));
        const ImportSettings* a = db2.GetSettings(db2.PathToGuid("audio/old.wav"));
        CHECK(a && a->audio.loadMode == AudioImportSettings::LoadMode::Stream && a->audio.forceMono);
        CHECK_NEAR(a->audio.quality, 0.25f, 1e-6);
        CHECK(Has(p.Read("audio/old.wav.okaymeta"), "importer audio 2"));
        // Migration is not a "settings changed" signal.
        for (const auto& e : r.reimports) CHECK(e.reason != ReimportEvent::Reason::SettingsChanged);
        // And it is stable: another scan rewrites nothing.
        const std::string stable = p.Read("tex/old.png.okaymeta");
        ScanReport r2 = db2.Scan(p.Root());
        CHECK(r2.upgraded == 0 && p.Read("tex/old.png.okaymeta") == stable);
    }

    // --- Hot reload hook ------------------------------------------------------------------------------------------
    {
        Project p("hot");
        p.Png("a.png", Color(1, 0, 0, 1));
        AssetDatabase db;
        db.Scan(p.Root());
        db.TakeReimports();
        AssetManager mgr;
        mgr.RegisterDefaultLoaders();
        int chained = 0;
        db.AttachHotReload(mgr, [&](const std::string&, const std::string&) { ++chained; });
        mgr.SetHotReload(true, 0.0);
        auto h = mgr.Get<Image>(p.Root() + "/a.png");
        CHECK(h.Ready());
        p.Png("a.png", Color(0, 0, 1, 1));
        Bump(p.P("a.png"), 5);
        bool got = false;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (std::chrono::steady_clock::now() < end && !got) {
            mgr.Pump();
            const auto ev = db.TakeReimports();
            for (const auto& e : ev) if (e.reason == ReimportEvent::Reason::SourceChanged && e.path == "a.png") got = true;
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(got);
        CHECK(chained >= 1);
        mgr.SetReloadCallback({});   // the database goes out of scope first
    }

    // --- Dependencies: graph, closure, unused, missing, cycles -----------------------------------------------------
    {
        Project p("deps");
        BuildGame(p);
        AssetDatabase db;
        db.Scan(p.Root());
        auto G = [&](const std::string& rel) { return db.PathToGuid(rel); };
        auto paths = [&](const std::vector<Guid>& v) {
            std::set<std::string> s;
            for (const Guid& g : v) s.insert(db.GuidToPath(g));
            return s;
        };
        using S = std::set<std::string>;
        const Guid main = G("scenes/main.okayscene");

        CHECK(paths(db.DependenciesOf(main)) == (S{"art/hero.png", "audio/boom.wav", "prefabs/enemy.okayprefab"}));
        CHECK(paths(db.DependenciesOf(main, true)) ==
              (S{"art/hero.png", "audio/boom.wav", "prefabs/enemy.okayprefab", "prefabs/enemy_tex.png", "art/enemy.png",
                 "materials/stone.okaymat", "art/stone.png"}));
        CHECK(paths(db.DependenciesOf(G("prefabs/enemy.okayprefab"))) ==
              (S{"prefabs/enemy_tex.png", "art/enemy.png", "materials/stone.okaymat"}));   // one is relative to the prefab
        CHECK(paths(db.DependenciesOf(G("materials/stone.okaymat"))) == (S{"art/stone.png"}));
        CHECK(paths(db.DependenciesOf(G("dialogue/intro.okaydialog"))) == (S{"art/npc.png", "audio/hi.wav"}));
        CHECK(paths(db.DependenciesOf(G("ai/guard.okaybt"))) == (S{"audio/alarm.wav", "art/bt.png"}));
        CHECK(paths(db.DependenciesOf(G("cutscenes/intro.okayseq"))) == (S{"audio/cue.wav"}));
        CHECK(paths(db.DependenciesOf(G("scenes/win.okayscene"))) == (S{"art/win.png"}));   // a backslash path
        CHECK(db.DependenciesOf(G("art/hero.png")).empty());
        CHECK(db.DependenciesOf(Guid::Generate()).empty());

        CHECK(paths(db.UsedBy(G("art/stone.png"))) == (S{"materials/stone.okaymat"}));
        CHECK(paths(db.UsedBy(G("art/stone.png"), true)) ==
              (S{"materials/stone.okaymat", "prefabs/enemy.okayprefab", "scenes/main.okayscene"}));
        CHECK(paths(db.UsedBy(G("prefabs/enemy_tex.png"))) == (S{"prefabs/enemy.okayprefab"}));
        CHECK(db.UsedBy(main).empty());
        CHECK(db.UsedBy(G("art/unused.png")).empty());

        // Closure for a build: the roots and everything they reach, once each.
        const std::vector<Guid> closure = db.BuildClosure({main});
        CHECK(closure.size() == 8);
        CHECK(closure[0] == main);
        CHECK(paths(closure).size() == 8);
        CHECK(paths(closure).count("art/unused.png") == 0 && paths(closure).count("art/win.png") == 0);
        CHECK(db.BuildClosure({main, main, G("art/hero.png")}).size() == 8);
        CHECK(db.BuildClosure({}).empty());
        CHECK(db.BuildClosure({Guid::Generate()}).empty());
        const auto cp = db.BuildClosurePaths({"scenes/main.okayscene"});
        CHECK(cp.size() == 8);
        CHECK(db.BuildClosurePaths({main.ToString(), "scenes/win.okayscene", "nonsense"}).size() == 10);

        // Cycles terminate.
        const Guid ca = G("prefabs/cyc_a.okayprefab"), cb = G("prefabs/cyc_b.okayprefab");
        CHECK(paths(db.BuildClosure({ca})) == (S{"prefabs/cyc_a.okayprefab", "prefabs/cyc_b.okayprefab"}));
        CHECK(db.BuildClosure({ca}).size() == 2);
        CHECK(paths(db.DependenciesOf(ca, true)) == (S{"prefabs/cyc_b.okayprefab"}));
        CHECK(paths(db.UsedBy(ca, true)) == (S{"prefabs/cyc_b.okayprefab"}));
        CHECK(Contains(db.UsedBy(cb), ca));

        // Unused: not reachable from the roots (a scene list, usually Build Settings).
        const auto unused = paths(db.FindUnused({main}));
        CHECK(unused.size() == static_cast<std::size_t>(kAssets) - 8);
        CHECK(unused.count("art/unused.png") && unused.count("audio/unused.wav") && unused.count("art/npc.png"));
        CHECK(!unused.count("art/hero.png") && !unused.count("art/stone.png") && !unused.count("scenes/main.okayscene"));
        const auto unusedMore = paths(db.FindUnused({main, G("dialogue/intro.okaydialog"), G("ai/guard.okaybt")}));
        CHECK(!unusedMore.count("art/npc.png") && !unusedMore.count("audio/alarm.wav") && unusedMore.count("art/unused.png"));
        const auto unusedScenes = paths(db.FindUnused({}));   // empty roots: every scene
        CHECK(!unusedScenes.count("art/win.png") && !unusedScenes.count("scenes/win.okayscene") && unusedScenes.count("art/unused.png"));
        CHECK(unusedScenes.size() == static_cast<std::size_t>(kAssets) - 10);
        const auto unref = paths(db.FindUnreferenced());
        CHECK(unref.count("scenes/main.okayscene") && unref.count("art/unused.png") && !unref.count("art/hero.png"));
        CHECK(!unref.count("prefabs/cyc_a.okayprefab"));

        // Missing references: exactly the two real ones (names, tags, sentences and URLs are not paths).
        const std::vector<MissingRef> missing = db.FindMissing();
        CHECK(missing.size() == 2);
        std::set<std::string> miss;
        for (const auto& m : missing) { CHECK(m.referrerPath == "scenes/main.okayscene" && m.referrer == main); miss.insert(m.reference); }
        CHECK(miss == (S{"art/missing.png", "scripts/none.okayvs"}));

        // Search by dependencies.
        AssetQuery q;
        q.referencing = G("art/stone.png");
        CHECK(db.Search(q).total == 1);
        q.transitive = true;
        CHECK(db.Search(q).total == 3);
        AssetQuery q2;
        q2.referencedBy = main;
        CHECK(db.Search(q2).total == 3);
        q2.transitive = true;
        CHECK(db.Search(q2).total == 7);
        AssetQuery q3;
        q3.unusedOnly = true;
        CHECK(db.Search(q3).total == unref.size());
        AssetQuery q4;
        q4.withMissingRefsOnly = true;
        const AssetPage mp = db.Search(q4);
        CHECK(mp.total == 1 && mp.items[0]->path == "scenes/main.okayscene");

        // A reference to a file that appears later stops being missing after the next scan.
        p.Png("art/missing.png", Color(0.3f, 0.3f, 0.3f, 1));
        db.Scan(p.Root());
        CHECK(db.FindMissing().size() == 1);
        CHECK(Contains(db.DependenciesOf(main), G("art/missing.png")));

        // A custom extractor for a custom file type.
        p.Text("lists/shop.list", "item=art/hero.png\nitem=\"art/enemy.png\"\nitem=art/ghost.png\n");
        db.RegisterExtractor("list", {"LIST"}, [](const std::string&, const std::string& text, std::vector<FoundRef>& out) {
            std::size_t pos = 0;
            while ((pos = text.find("item=", pos)) != std::string::npos) {
                pos += 5;
                std::size_t end = text.find('\n', pos);
                if (end == std::string::npos) end = text.size();
                std::string v = text.substr(pos, end - pos);
                FoundRef r;
                r.begin = pos; r.end = end;
                if (!v.empty() && v.front() == '"') { v = v.substr(1, v.size() - 2); r.begin += 1; r.end -= 1; r.quoted = true; }
                r.path = v;
                out.push_back(r);
                pos = end;
            }
        });
        db.Scan(p.Root());
        CHECK(paths(db.DependenciesOf(G("lists/shop.list"))) == (S{"art/hero.png", "art/enemy.png"}));
        CHECK(db.FindMissing().size() == 2);   // ghost.png
        CHECK(db.RemoveExtractor("list") && !db.RemoveExtractor("list"));
        db.RebuildDependencies();
        CHECK(db.DependenciesOf(G("lists/shop.list")).empty());
        CHECK(db.ExtractorNames().size() >= 3);

        // A shipped (packed) scene is read through DataPack.
        p.Text("scenes/packed.okayscene", DataPack::Pack("okayscene 1\ngameobject 0 \"P\"\n  sprite \"art/enemy.png\" 1\nend\n"));
        db.Scan(p.Root());
        CHECK(paths(db.DependenciesOf(G("scenes/packed.okayscene"))) == (S{"art/enemy.png"}));
    }

    // --- References survive a move; stale paths can be rewritten ----------------------------------------------------
    {
        Project p("stale");
        BuildGame(p);
        AssetDatabase db;
        db.Scan(p.Root());
        db.TakeReimports();
        const Guid hero = db.PathToGuid("art/hero.png");
        const Guid main = db.PathToGuid("scenes/main.okayscene");
        const std::string sceneBefore = p.Read("scenes/main.okayscene");

        p.Move("art/hero.png", "art/characters/hero.png");
        p.Move("prefabs/enemy_tex.png", "prefabs/tex/enemy_tex.png");
        db.Scan(p.Root());
        CHECK(db.GuidToPath(hero) == "art/characters/hero.png");
        // Nothing the move touched turns up as missing; the dependency edges are intact.
        CHECK(db.FindMissing().size() == 2);
        CHECK(Contains(db.UsedBy(hero), main));
        CHECK(Contains(db.DependenciesOf(main), hero));
        CHECK(db.BuildClosure({main}).size() == 8);
        const std::vector<StaleRef> stale = db.FindStaleReferences();
        CHECK(stale.size() == 2);
        bool sawHero = false, sawTex = false;
        for (const auto& s : stale) {
            if (s.reference == "art/hero.png" && s.referrerPath == "scenes/main.okayscene" && s.target == hero &&
                s.targetPath == "art/characters/hero.png") sawHero = true;
            if (s.reference == "enemy_tex.png" && s.referrerPath == "prefabs/enemy.okayprefab" && s.targetPath == "prefabs/tex/enemy_tex.png") sawTex = true;
        }
        CHECK(sawHero && sawTex);

        std::vector<std::string> changed;
        CHECK(db.FixStaleReferences(&changed) == 2);
        CHECK(changed.size() == 2);
        const std::string sceneAfter = p.Read("scenes/main.okayscene");
        CHECK(Has(sceneAfter, "\"art/characters/hero.png\"") && !Has(sceneAfter, "\"art/hero.png\""));
        // Only that path changed.
        std::string undone = sceneAfter;
        const std::size_t at = undone.find("art/characters/hero.png");
        if (at != std::string::npos) undone.replace(at, std::string("art/characters/hero.png").size(), "art/hero.png");
        CHECK(undone == sceneBefore);
        CHECK(Has(p.Read("prefabs/enemy.okayprefab"), "\"tex/enemy_tex.png\""));   // still relative to the prefab
        CHECK(db.FindStaleReferences().empty());
        CHECK(db.FindMissing().size() == 2);
        CHECK(db.BuildClosure({main}).size() == 8);
        // The rewritten files were signalled and their metas hold the new hash.
        int sourceChanged = 0;
        for (const auto& e : db.TakeReimports()) if (e.reason == ReimportEvent::Reason::SourceChanged) ++sourceChanged;
        CHECK(sourceChanged == 2);
        CHECK(db.Find(main)->hash == AssetDatabase::HashFile(db.AbsolutePath(main)));
        ScanReport quiet = db.Scan(p.Root());
        CHECK(quiet.reimports.empty());
        CHECK(db.FixStaleReferences() == 0);

        // The editor-style API: move through the database, the old path keeps working.
        const Guid stone = db.PathToGuid("art/stone.png");
        std::string err;
        CHECK(!db.MoveAsset("art/stone.png", "art/characters/hero.png", &err) && !err.empty());   // target taken
        CHECK(!db.MoveAsset("art/stone.png", "../outside.png", &err));
        CHECK(!db.MoveAsset("art/stone.png", "art/stone.wav", &err));                           // would change the type
        CHECK(!db.MoveAsset("nothing.png", "x.png", &err));
        CHECK(db.MoveAsset("art/stone.png", "art/rocks/stone.png", &err));
        CHECK(p.Exists("art/rocks/stone.png") && p.Exists("art/rocks/stone.png.okaymeta"));
        CHECK(!p.Exists("art/stone.png") && !p.Exists("art/stone.png.okaymeta"));
        CHECK(db.PathToGuid("art/rocks/stone.png") == stone);
        CHECK(db.Resolve("art/stone.png").viaRedirect);
        CHECK(Has(p.Read("art/rocks/stone.png.okaymeta"), "guid " + stone.ToString()));
        CHECK(Contains(db.UsedBy(stone), db.PathToGuid("materials/stone.okaymat")));
        CHECK(db.FindStaleReferences().size() == 1);
        CHECK(db.FixStaleReferences() == 1);
        CHECK(Has(p.Read("materials/stone.okaymat"), "texture art/rocks/stone.png\n"));   // a bare token, no quotes
        CHECK(db.FindStaleReferences().empty());
        // A fresh scan sees the same world.
        AssetDatabase again;
        again.Scan(p.Root());
        CHECK(again.PathToGuid("art/rocks/stone.png") == stone);
        CHECK(again.FindMissing().size() == 2);
        CHECK(again.FindStaleReferences().empty());
    }

    // --- References resolve with the project folder's own name in front -----------------------------------------------
    {
        Project p("rootname");
        p.Png("Assets/art/hero.png", Color(1, 0, 0, 1));
        p.Text("Assets/scenes/main.okayscene", "okayscene 1\ngameobject 0 \"A\"\n  sprite \"Assets/art/hero.png\" 1\n  audio \"Assets/audio/gone.wav\" 1\nend\n");
        AssetDatabase inner;
        inner.Scan(p.Root() + "/Assets");
        const Guid sc = inner.PathToGuid("scenes/main.okayscene");
        CHECK(inner.DependenciesOf(sc).size() == 1);
        CHECK(inner.FindMissing().size() == 1);
        AssetDatabase outer;
        outer.Scan(p.Root());
        const Guid sc2 = outer.PathToGuid("Assets/scenes/main.okayscene");
        CHECK(outer.DependenciesOf(sc2).size() == 1);
        CHECK(outer.FindMissing().size() == 1);
    }

    // --- The scene extractor finds what SceneSerializer::CollectAssetPaths finds (real serializer output) ---------------
    {
        Scene scene("Parity");
        GameObject* go = scene.CreateGameObject("Everything");
        go->AddComponent<SpriteRenderer>()->texture = "art/sprite.png";
        go->AddComponent<AudioSource>()->clipPath = "audio/clip.wav";
        go->AddComponent<MusicPlayer>()->tracks = {"audio/m1.ogg", "audio/m2.mp3"};
        auto* mr = go->AddComponent<MeshRenderer>();
        mr->meshPath = "models/mesh.glb";
        mr->texture = "art/mesh_tex.png";
        mr->normalMap = "art/mesh_n.png";
        mr->specularMap = "art/mesh_s.png";
        mr->aoMap = "art/mesh_ao.png";
        GameObject* t = scene.CreateGameObject("Terrain");
        auto* ter = t->AddComponent<Terrain>();
        ter->texture = "art/ter.png";
        ter->normalMap = "art/ter_n.png";
        GameObject* ps = scene.CreateGameObject("Particles");
        ps->AddComponent<ParticleSystem>()->texture = "art/spark.png";
        GameObject* ui = scene.CreateGameObject("UI");
        ui->AddComponent<UIImage>()->texture = "ui/image.png";
        ui->AddComponent<UIRawImage>()->texture = "ui/raw.png";
        ui->AddComponent<UIButton>()->icon = "ui/button.png";
        GameObject* an = scene.CreateGameObject("Anim");
        auto* sa = an->AddComponent<SpriteAnimator>();
        sa->frames = {"art/f1.png", "art/f2.png"};
        GameObject* seq = scene.CreateGameObject("Seq");
        seq->AddComponent<SequencePlayer>()->sequencePath = "cutscenes/intro.okayseq";
        GameObject* sp = scene.CreateGameObject("Spawner");
        sp->AddComponent<LogicTarget>()->spawnPrefab = "prefabs/spawn.okayprefab";
        GameObject* nav = scene.CreateGameObject("Nav");
        nav->AddComponent<NavMeshSurface>()->navPath = "nav/level.okaynav";
        GameObject* ai = scene.CreateGameObject("AI");
        ai->AddComponent<BehaviorTreeRunner>()->treePath = "ai/guard.okaybt";
        GameObject* sm = scene.CreateGameObject("Rail");
        sm->AddComponent<SplineMesh>()->sourceMesh = "models/rail.obj";
        sm->AddComponent<SplineInstancer>()->prefabPath = "prefabs/post.okayprefab";
        sm->AddComponent<LineRenderer>()->texture = "art/line.png";
        GameObject* inst = scene.CreateGameObject("Instance");
        inst->prefabSource = "prefabs/source.okayprefab";

        const std::string text = SceneSerializer::Serialize(scene);
        std::set<std::string> found;
        for (const FoundRef& r : ExtractSceneRefs(text)) found.insert(r.path);
        std::vector<std::string> expected = SceneSerializer::CollectAssetPaths(scene);
        expected.push_back("prefabs/source.okayprefab");   // the prefab link on the object itself
        CHECK(expected.size() >= 25);
        for (const std::string& e : expected) {
            const bool ok = found.count(e) != 0;
            if (!ok) std::cerr << "  (scene extractor missed " << e << ")\n";
            CHECK(ok);
        }
        // And it invents nothing: every ref is one of those.
        for (const std::string& f : found) {
            const bool known = std::find(expected.begin(), expected.end(), f) != expected.end();
            if (!known) std::cerr << "  (scene extractor invented " << f << ")\n";
            CHECK(known);
        }
        // Spans point at the text they came from.
        for (const FoundRef& r : ExtractSceneRefs(text)) CHECK(r.begin < r.end && text.compare(r.begin, r.end - r.begin, r.path) == 0);
    }

    // --- Search, filter, sort and page --------------------------------------------------------------------------------
    {
        Project p("search");
        p.Png("art/Hero.png", Color(1, 0, 0, 1), 4, 4);
        p.Png("art/hero_icon.png", Color(0, 1, 0, 1), 8, 8);
        p.Png("art/ui/button.png", Color(0, 0, 1, 1), 16, 16);
        p.Png("art/ui/panel.png", Color(1, 1, 0, 1), 2, 2);
        p.Text("audio/boom.wav", "RIFF-boom-long-long-long");
        p.Text("audio/music/theme.ogg", "OggS");
        p.Text("scenes/main.okayscene", "okayscene 1\n  sprite \"art/Hero.png\" 1\n");
        p.Text("prefabs/hero.okayprefab", "okayscene 1\n  sprite \"art/hero_icon.png\" 1\n");
        SetAge(p.P("art/Hero.png"), 72);
        SetAge(p.P("art/ui/button.png"), 24);
        SetAge(p.P("art/ui/panel.png"), 2);
        AssetDatabase db;
        db.Scan(p.Root());
        auto names = [](const AssetPage& pg) {
            std::vector<std::string> v;
            for (const AssetRecord* r : pg.items) v.push_back(r->Name());
            return v;
        };
        using V = std::vector<std::string>;
        CHECK(db.Count() == 8);
        CHECK(db.AddLabel(db.PathToGuid("art/Hero.png"), "player"));
        CHECK(db.AddLabel(db.PathToGuid("art/Hero.png"), "UI"));
        CHECK(db.AddLabel(db.PathToGuid("art/ui/button.png"), "ui"));

        AssetQuery all;
        const AssetPage everything = db.Search(all);
        CHECK(everything.total == 8 && everything.items.size() == 8 && everything.pageCount == 1);
        CHECK(names(everything) == (V{"boom.wav", "button.png", "hero.okayprefab", "Hero.png", "hero_icon.png", "main.okayscene", "panel.png", "theme.ogg"}));
        all.descending = true;
        CHECK(names(db.Search(all)).front() == "theme.ogg" && names(db.Search(all)).back() == "boom.wav");

        AssetQuery q;
        q.text = "HERO";
        CHECK(names(db.Search(q)) == (V{"hero.okayprefab", "Hero.png", "hero_icon.png"}));
        q.text = "art/ui";
        CHECK(db.Search(q).total == 0);              // names only by default
        q.matchFolders = true;
        CHECK(names(db.Search(q)) == (V{"button.png", "panel.png"}));

        AssetQuery k;
        k.types = {AssetType::Texture};
        CHECK(db.Search(k).total == 4);
        k.types = {AssetType::Audio, AssetType::Scene};
        CHECK(db.Search(k).total == 3);
        AssetQuery e;
        e.extensions = {".PNG", "ogg"};
        CHECK(db.Search(e).total == 5);

        AssetQuery l;
        l.labels = {"UI"};
        CHECK(names(db.Search(l)) == (V{"button.png", "Hero.png"}));
        l.labels = {"ui", "player"};
        CHECK(names(db.Search(l)) == (V{"Hero.png"}));
        l.anyLabel = true;
        CHECK(db.Search(l).total == 2);
        l.labels = {"nothing"};
        CHECK(db.Search(l).total == 0);

        AssetQuery f;
        f.folder = "art";
        CHECK(db.Search(f).total == 4);
        f.recursive = false;
        CHECK(names(db.Search(f)) == (V{"Hero.png", "hero_icon.png"}));
        f.folder = "art\\ui\\";
        f.recursive = true;
        CHECK(db.Search(f).total == 2);
        f.folder = "ar";
        CHECK(db.Search(f).total == 0);              // a folder, not a prefix

        AssetQuery m;
        m.modifiedWithinSeconds = 3 * 3600;
        const auto recent = names(db.Search(m));
        CHECK(recent.size() == 6);
        CHECK(std::find(recent.begin(), recent.end(), "Hero.png") == recent.end());
        CHECK(std::find(recent.begin(), recent.end(), "button.png") == recent.end());
        CHECK(std::find(recent.begin(), recent.end(), "panel.png") != recent.end());
        AssetQuery m2;
        m2.modifiedWithinSeconds = 30 * 3600;
        CHECK(db.Search(m2).total == 7);
        AssetQuery m3;
        m3.modifiedAfter = db.Find("art/ui/panel.png")->modified;
        CHECK(db.Search(m3).total == 6);
        AssetQuery m4;                                // an explicit "now" makes it exact
        m4.now = db.Find("art/Hero.png")->modified + 3600;
        m4.modifiedWithinSeconds = 1800;              // Hero.png is 3600 s old at that "now"
        CHECK(db.Search(m4).total == 7);
        m4.modifiedWithinSeconds = 7200;
        CHECK(db.Search(m4).total == 8);

        AssetQuery s;
        s.sort = AssetQuery::Sort::Modified;
        CHECK(names(db.Search(s)).front() == "Hero.png");             // oldest first
        CHECK(names(db.Search(s))[1] == "button.png");
        s.descending = true;
        CHECK(names(db.Search(s)).back() == "Hero.png");
        AssetQuery sk;
        sk.sort = AssetQuery::Sort::Type;
        CHECK(names(db.Search(sk)).front() == "button.png");          // textures first, then by name
        CHECK(names(db.Search(sk)).back() == "hero.okayprefab");
        AssetQuery ss;
        ss.sort = AssetQuery::Sort::Size;
        CHECK(names(db.Search(ss)).back().size() > 0);
        std::int64_t prev = -1;
        bool ascending = true;
        for (const AssetRecord* r : db.Search(ss).items) { if (r->size < prev) ascending = false; prev = r->size; }
        CHECK(ascending);
        AssetQuery sp;
        sp.sort = AssetQuery::Sort::Path;
        CHECK(names(db.Search(sp)).front() == "Hero.png");            // art/Hero.png < art/hero_icon.png (case folded) < ...
        CHECK(db.Search(sp).items.front()->path == "art/Hero.png");

        // Pages.
        AssetQuery pg;
        pg.pageSize = 3;
        const AssetPage p0 = db.Search(pg);
        pg.page = 1;
        const AssetPage p1 = db.Search(pg);
        pg.page = 2;
        const AssetPage p2 = db.Search(pg);
        pg.page = 99;
        const AssetPage pLast = db.Search(pg);
        CHECK(p0.total == 8 && p0.pageCount == 3 && p0.items.size() == 3 && p1.items.size() == 3 && p2.items.size() == 2);
        CHECK(pLast.page == 2 && names(pLast) == names(p2));
        V joined = names(p0);
        for (const auto& n : names(p1)) joined.push_back(n);
        for (const auto& n : names(p2)) joined.push_back(n);
        CHECK(joined == names(everything));
        AssetQuery none;
        none.text = "zzz";
        none.pageSize = 5;
        const AssetPage empty = db.Search(none);
        CHECK(empty.total == 0 && empty.items.empty() && empty.pageCount == 1 && empty.page == 0);

        // Facets for the filter bar.
        const auto counts = db.CountByType();
        CHECK(counts.at(AssetType::Texture) == 4 && counts.at(AssetType::Audio) == 2 &&
              counts.at(AssetType::Scene) == 1 && counts.at(AssetType::Prefab) == 1);
        AssetQuery cq;
        cq.text = "hero";
        const auto hc = db.CountByType(cq);
        CHECK(hc.at(AssetType::Texture) == 2 && hc.at(AssetType::Prefab) == 1 && hc.size() == 2);
        const auto labels = db.AllLabels();
        CHECK(labels.size() == 2 && labels[0].first == "player" && labels[0].second == 1 && labels[1].first == "UI" && labels[1].second == 2);

        // Labels: de-duplicated, trimmed, stored in the meta, removable.
        const Guid hero = db.PathToGuid("art/Hero.png");
        CHECK(db.SetLabels(hero, {"  alpha ", "ALPHA", "", "be\nta"}));
        CHECK(db.Find(hero)->labels == (V{"alpha", "be ta"}));
        CHECK(Has(p.Read("art/Hero.png.okaymeta"), "label alpha\n") && Has(p.Read("art/Hero.png.okaymeta"), "label be ta\n"));
        CHECK(db.RemoveLabel(hero, "Alpha"));
        CHECK(db.Find(hero)->labels == (V{"be ta"}));
        CHECK(!db.AddLabel(Guid::Generate(), "x"));
        // Labels survive a new database (they live in the meta).
        AssetDatabase db2;
        db2.Scan(p.Root());
        CHECK(db2.Find("art/Hero.png")->labels == (V{"be ta"}));
        CHECK(db2.Find("art/ui/button.png")->HasLabel("UI"));
    }

    // --- Box downscale and placeholders ----------------------------------------------------------------------------------
    {
        Image src(4, 4);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) {
                const bool white = ((x / 1) + (y / 1)) % 2 == 1;
                std::uint8_t* px = src.Data() + (y * 4 + x) * 4;
                px[0] = px[1] = px[2] = white ? 255 : 0;
                px[3] = 255;
            }
        Image half = BoxDownscale(src, 2);
        CHECK(half.Width() == 2 && half.Height() == 2);
        for (int i = 0; i < 4; ++i) CHECK(std::abs(static_cast<int>(half.Data()[i * 4]) - 128) <= 1);

        // Transparent pixels do not drag the colour down.
        Image two(2, 2);
        for (int i = 0; i < 4; ++i) {
            std::uint8_t* px = two.Data() + i * 4;
            const bool solid = i % 2 == 0;
            px[0] = solid ? 255 : 0; px[1] = 0; px[2] = 0; px[3] = solid ? 255 : 0;
        }
        Image one = BoxDownscale(two, 1);
        CHECK(one.Width() == 1 && one.Height() == 1);
        CHECK(one.Data()[0] == 255 && one.Data()[1] == 0 && std::abs(static_cast<int>(one.Data()[3]) - 128) <= 1);

        // Odd sizes keep the aspect ratio and a flat colour stays flat; small images are not enlarged.
        Image flat(100, 37);
        flat.Fill(Color(10 / 255.0f, 20 / 255.0f, 30 / 255.0f, 1));
        Image fs2 = BoxDownscale(flat, 10);
        CHECK(fs2.Width() == 10 && fs2.Height() == 4);
        for (int y = 0; y < fs2.Height(); ++y)
            for (int x = 0; x < fs2.Width(); ++x) {
                const Color c = fs2.GetPixel(x, y);
                CHECK_NEAR(c.r * 255.0f, 10.0f, 1.0);
                CHECK_NEAR(c.b * 255.0f, 30.0f, 1.0);
            }
        Image smallImg(8, 8);
        CHECK(BoxDownscale(smallImg, 64).Width() == 8);
        CHECK(!BoxDownscale(Image(), 8).Valid());

        const Image a = MakePlaceholderThumbnail(AssetType::Scene, "okayscene", 64);
        const Image b = MakePlaceholderThumbnail(AssetType::Texture, "png", 64);
        const Image c = MakePlaceholderThumbnail(AssetType::Scene, "okayscene", 64, true);
        CHECK(a.Valid() && a.Width() == 64 && a.Height() == 64);
        CHECK(a.Pixels() != b.Pixels() && a.Pixels() != c.Pixels());
        CHECK(MakePlaceholderThumbnail(AssetType::Other, "", 8).Width() == 16);   // tiny sizes are lifted
    }

    // --- Thumbnail cache --------------------------------------------------------------------------------------------------------
    {
        Project p("thumbs");
        p.Png("big.png", Color(1, 0, 0, 1), 256, 128);
        p.Png("tall.png", Color(0, 1, 0, 1), 100, 400);
        p.Png("small.png", Color(0, 0, 1, 1), 16, 16);
        p.Text("scenes/a.okayscene", "okayscene 1\n");
        p.Text("junk.png", "this is not an image");
        AssetDatabase db;
        db.Scan(p.Root());
        auto G = [&](const std::string& rel) { return db.PathToGuid(rel); };

        ThumbnailCache::Options o;
        o.size = 64;
        o.diskDir = p.Root() + "/.thumbs";             // a hidden folder: the scan skips it
        ThumbnailCache tc(o);

        auto t = tc.Get(db, G("big.png"));
        CHECK(t.source == ThumbnailCache::Source::Generated && !t.placeholder && !t.failed);
        CHECK(t.image && t.image->Width() == 64 && t.image->Height() == 32);
        {
            const Color c = t.image->GetPixel(10, 10);
            CHECK(c.r > 0.98f && c.g < 0.02f && c.b < 0.02f);
        }
        CHECK(tc.GetStats().generated == 1 && tc.GetStats().diskWrites == 1 && tc.GetStats().memoryHits == 0);
        CHECK(tc.InMemory(G("big.png"), db.Find("big.png")->hash));
        CHECK(tc.OnDisk(G("big.png"), db.Find("big.png")->hash));
        CHECK(Has(tc.DiskPathFor(G("big.png"), 1), G("big.png").ToString().substr(0, 2) + "/" + G("big.png").ToString()));

        // A second request is a memory hit and the same picture.
        auto t2 = tc.Get(db, G("big.png"));
        CHECK(t2.source == ThumbnailCache::Source::Memory && t2.image == t.image);
        CHECK(tc.GetStats().memoryHits == 1 && tc.GetStats().generated == 1);

        // Tall images keep their shape; small ones are not enlarged.
        auto tall = tc.Get(db, G("tall.png"));
        CHECK(tall.image->Width() == 16 && tall.image->Height() == 64);
        auto small = tc.Get(db, G("small.png"));
        CHECK(small.image->Width() == 16 && small.image->Height() == 16);

        // A new cache over the same folder reads the PNG back instead of decoding the source.
        {
            ThumbnailCache warm(o);
            auto w = warm.Get(db, G("big.png"));
            CHECK(w.source == ThumbnailCache::Source::Disk && !w.placeholder);
            CHECK(w.image->Width() == 64 && w.image->Height() == 32);
            CHECK(warm.GetStats().diskHits == 1 && warm.GetStats().generated == 0);
            auto w2 = warm.Get(db, G("big.png"));
            CHECK(w2.source == ThumbnailCache::Source::Memory);
        }
        // A cache with another tag misses (the generator changed).
        {
            ThumbnailCache::Options o2 = o;
            o2.tag = "2";
            ThumbnailCache other(o2);
            CHECK(other.Get(db, G("big.png")).source == ThumbnailCache::Source::Generated);
        }
        // Without a disk folder everything is regenerated per cache.
        {
            ThumbnailCache::Options o3 = o;
            o3.diskDir.clear();
            ThumbnailCache mem(o3);
            CHECK(mem.Get(db, G("big.png")).source == ThumbnailCache::Source::Generated);
            CHECK(mem.GetStats().diskWrites == 0 && !mem.OnDisk(G("big.png"), 1));
        }

        // Types without a generator get a placeholder, shared per type; unreadable images get an error tile.
        auto sc = tc.Get(db, G("scenes/a.okayscene"));
        CHECK(sc.placeholder && !sc.failed && sc.source == ThumbnailCache::Source::Placeholder);
        CHECK(sc.image->Width() == 64 && sc.image->Height() == 64);
        CHECK(tc.Get(db, G("scenes/a.okayscene")).placeholder);
        CHECK(tc.GetStats().placeholders == 1);
        auto junk = tc.Get(db, G("junk.png"));
        CHECK(junk.placeholder && junk.failed);
        CHECK(tc.GetStats().failures == 1);
        auto junk2 = tc.Get(db, G("junk.png"));          // the failure is remembered, not retried
        CHECK(junk2.failed && junk2.source == ThumbnailCache::Source::Memory && tc.GetStats().failures == 1);
        CHECK(junk.image->Pixels() != sc.image->Pixels());
        CHECK(tc.Get(db, Guid::Generate()).failed);       // an unknown asset

        // The source changed: a new content hash is a miss, and the stale PNG is cleaned up.
        const Guid big = G("big.png");
        const std::uint64_t oldHash = db.Find(big)->hash;
        p.Png("big.png", Color(0, 0, 1, 1), 128, 128);
        Bump(p.P("big.png"), 5);
        db.RefreshFile("big.png");
        CHECK(db.Find(big)->hash != oldHash);
        auto fresh = tc.Get(db, big);
        CHECK(fresh.source == ThumbnailCache::Source::Generated);
        CHECK(fresh.image->Width() == 64 && fresh.image->Height() == 64);
        {
            const Color c = fresh.image->GetPixel(5, 5);
            CHECK(c.b > 0.98f && c.r < 0.02f);
        }
        CHECK(!tc.OnDisk(big, oldHash) && tc.OnDisk(big, db.Find(big)->hash));
        int files = 0;
        for (const auto& e : fs::directory_iterator(fs::path(o.diskDir) / big.ToString().substr(0, 2)))
            if (e.path().filename().string().compare(0, 32, big.ToString()) == 0) ++files;
        CHECK(files == 1);

        // Invalidate forgets one asset everywhere.
        tc.Invalidate(big);
        CHECK(!tc.InMemory(big, db.Find(big)->hash) && !tc.OnDisk(big, db.Find(big)->hash));
        CHECK(tc.InMemory(G("tall.png"), db.Find("tall.png")->hash));

        // Custom generators: asked newest first; one for a new type, one that overrides, one that throws.
        ThumbnailCache::Options od = o;
        od.diskDir.clear();                              // so the PNGs written above can not answer for the generators
        ThumbnailCache cu(od);
        cu.AddGenerator({"okayscene"}, [](const std::string&) { Image i(10, 10); i.Fill(Color(0, 1, 0, 1)); return i; });
        auto cs = cu.Get(db, G("scenes/a.okayscene"));
        CHECK(!cs.placeholder && cs.source == ThumbnailCache::Source::Generated && cs.image->Width() == 10);
        cu.AddGenerator({".png"}, [](const std::string&) { Image i(4, 4); i.Fill(Color(1, 1, 0, 1)); return i; });
        auto cp = cu.Get(db, G("small.png"));
        CHECK(cp.image->Width() == 4);                    // the newest generator wins over the built-in
        ThumbnailCache bad(od);
        bad.AddGenerator({"okayscene"}, [](const std::string&) -> Image { throw 1; });
        auto bs = bad.Get(db, G("scenes/a.okayscene"));
        CHECK(bs.failed && bs.placeholder);

        // LRU: three images through a two-entry cache evict the oldest, which is still on disk.
        ThumbnailCache::Options o4 = o;
        o4.maxEntries = 2;
        ThumbnailCache lru(o4);
        lru.Get(db, G("tall.png"));
        lru.Get(db, G("small.png"));
        lru.Get(db, G("tall.png"));                       // tall is now the most recent
        lru.Get(db, G("junk.png"));                       // evicts small
        CHECK(lru.MemoryCount() == 2);
        CHECK(lru.GetStats().evictions >= 1);
        CHECK(lru.InMemory(G("tall.png"), db.Find("tall.png")->hash));
        CHECK(!lru.InMemory(G("small.png"), db.Find("small.png")->hash));
        CHECK(lru.Get(db, G("small.png")).source == ThumbnailCache::Source::Disk);
        // A byte budget evicts too, but always keeps the newest.
        ThumbnailCache::Options o5 = o;
        o5.maxBytes = 1;
        ThumbnailCache tiny(o5);
        tiny.Get(db, G("tall.png"));
        tiny.Get(db, G("small.png"));
        CHECK(tiny.MemoryCount() == 1);
        tiny.Clear();
        CHECK(tiny.MemoryCount() == 0 && tiny.MemoryBytes() == 0);
        tiny.ClearDisk();
        CHECK(!tiny.OnDisk(G("tall.png"), db.Find("tall.png")->hash));
    }

    // --- Scan options and robustness -----------------------------------------------------------------------------------
    {
        Project p("options");
        p.Png("art/a.png", Color(1, 0, 0, 1));
        p.Png("Library/cache.png", Color(0, 0, 1, 1));
        p.Png("node_modules/x/y.png", Color(0, 1, 0, 1));
        p.Text("notes.bak", "x");
        p.Text("art/a.png.part", "x");
        AssetDatabase db;
        ScanOptions o;
        o.ignoreDirs = {"Library", "node_modules"};
        ScanReport r = db.Scan(p.Root(), o);
        CHECK(r.scanned == 1);                              // folders by name, .bak / .part by extension
        CHECK(!p.Exists("Library/cache.png.okaymeta") && !p.Exists("notes.bak.okaymeta"));

        // Another root starts over; Rescan repeats the last scan; Clear forgets.
        Project q("options2");
        q.Png("z.png", Color(1, 0, 0, 1));
        db.Scan(q.Root());
        CHECK(db.Count() == 1 && db.Find("art/a.png") == nullptr && db.Find("z.png") != nullptr);
        q.Png("y.png", Color(0, 1, 0, 1));
        ScanReport rr = db.Rescan();
        CHECK(rr.created == 1 && db.Count() == 2);
        db.Clear();
        CHECK(db.Count() == 0 && db.Rescan().scanned == 0);
        ScanReport bad = db.Scan(p.Root() + "/nope");
        CHECK(!bad.warnings.empty() && db.Count() == 0);
    }
    {
        // Metas with Windows line endings and loose spacing (git autocrlf) still read.
        Project p("crlf");
        p.Png("a.png", Color(1, 0, 0, 1));
        p.Text("a.png.okaymeta", "okaymeta 1\r\nguid  AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA \r\ntype texture\r\nlabel crlf\r\n");
        AssetDatabase db;
        db.Scan(p.Root());
        CHECK(db.PathToGuid("a.png").ToString() == std::string(32, 'a'));
        CHECK(db.Find("a.png")->HasLabel("crlf"));
    }
    {
        // verifyHashes = false trusts the stored hash when the size matches; the default checks the content.
        Project p("trust");
        p.Text("a.wav", "RIFF-aaaa");
        { AssetDatabase d1; d1.Scan(p.Root()); }
        p.Text("a.wav", "RIFF-bbbb");
        Bump(p.P("a.wav"), 5);
        ScanOptions trust;
        trust.verifyHashes = false;
        AssetDatabase d2;
        CHECK(d2.Scan(p.Root(), trust).reimports.empty());
        AssetDatabase d3;
        const ScanReport r3 = d3.Scan(p.Root());
        CHECK(r3.reimports.size() == 1 && r3.reimports[0].reason == ReimportEvent::Reason::SourceChanged);
    }
    {
        // storeHashInMeta = false keeps the hash out of the file; a move is still followed within the session.
        Project p("nohash");
        p.Png("a.png", Color(1, 0, 0, 1));
        ScanOptions o;
        o.storeHashInMeta = false;
        AssetDatabase db;
        db.Scan(p.Root(), o);
        CHECK(!Has(p.Read("a.png.okaymeta"), "hash "));
        const Guid a = db.PathToGuid("a.png");
        p.Move("a.png", "b/a2.png", /*withMeta=*/false);
        const ScanReport r = db.Scan(p.Root(), o);
        CHECK(r.moved.size() == 1 && r.moved[0].guid == a && r.moved[0].how == "hash");
        CHECK(db.PathToGuid("b/a2.png") == a && p.Exists("b/a2.png.okaymeta") && !Has(p.Read("b/a2.png.okaymeta"), "hash "));
    }
    {
        // glTF files point at their buffers and images; data URIs are not files.
        Project p("gltf");
        p.Png("tex/a.png", Color(1, 0, 0, 1));
        p.Text("tex/mesh.bin", "BIN");
        p.Text("m.gltf", "{\"buffers\":[{\"uri\":\"tex/mesh.bin\"}],\"images\":[{\"uri\":\"tex/a.png\"},{\"uri\":\"data:image/png;base64,AAAA\"},{\"uri\":\"tex/gone.png\"}]}");
        AssetDatabase db;
        db.Scan(p.Root());
        const auto deps = db.DependenciesOf(db.PathToGuid("m.gltf"));
        CHECK(deps.size() == 2 && Contains(deps, db.PathToGuid("tex/a.png")) && Contains(deps, db.PathToGuid("tex/mesh.bin")));
        const auto miss = db.FindMissing();
        CHECK(miss.size() == 1 && miss[0].reference == "tex/gone.png");
    }

    RUN_SUITE("indexUnmetaed: every asset is indexed in memory and nothing is written");
    {
        Project p("virtual");
        p.Png("art/hero.png", Color(1, 0, 0, 1));
        p.Text("scenes/a.okayscene", "scene \"A\"\ngameobject \"o\"\n  sprite texture \"art/hero.png\"\n");
        p.Text("empty/.keep", "");
        std::error_code mk; fs::create_directories(p.P("empty/inner"), mk);
        AssetDatabase db;
        ScanOptions o; o.createMetas = false; o.indexUnmetaed = true; o.logWarnings = false;
        ScanReport r = db.Scan(p.Root(), o);
        CHECK(db.Count() == 2 && r.created == 0 && r.unmetaed == 2 && r.foldersChanged);
        CHECK(!p.Exists("art/hero.png.okaymeta") && !p.Exists("scenes/a.okayscene.okaymeta"));
        const Guid hero = db.PathToGuid("art/hero.png");
        CHECK(!hero.IsNull() && db.Find(hero)->metaVersion == 0 && db.UsedBy(hero).size() == 1);   // the graph works without metas
        CHECK((db.Folders() == std::vector<std::string>{"art", "empty", "empty/inner", "scenes"}));
        const std::uint64_t rev = db.Revision();
        r = db.Scan(p.Root(), o);
        CHECK(db.Revision() == rev && !r.foldersChanged && r.reimports.empty() && db.PathToGuid("art/hero.png") == hero);   // nothing changed: nothing moves
        p.Png("art/new.png", Color(0, 1, 0, 1));
        db.Scan(p.Root(), o);
        CHECK(db.Revision() > rev && db.Count() == 3);
        // a moved file keeps its GUID by content, still without a meta
        p.Move("art/hero.png", "art/champ.png", false);
        r = db.Scan(p.Root(), o);
        CHECK(r.moved.size() == 1 && r.moved[0].how == "hash" && db.PathToGuid("art/champ.png") == hero && !p.Exists("art/champ.png.okaymeta"));
        CHECK(db.UsedBy(hero).size() == 1 && db.FindStaleReferences().size() == 1);   // the scene still says art/hero.png: stale, not lost
        // the meta is written only on request, with the GUID the asset already had
        CHECK(db.EnsureMeta(hero) && p.Exists("art/champ.png.okaymeta") && db.Find(hero)->metaVersion > 0);
        AssetDatabase again;
        again.Scan(p.Root(), o);
        CHECK(again.PathToGuid("art/champ.png") == hero);                            // and now it survives a new session
        // an in-memory asset is not written by a refresh either
        const Guid nw = db.PathToGuid("art/new.png");
        p.Png("art/new.png", Color(0, 0, 1, 1), 9, 9);
        db.RefreshFile("art/new.png");
        CHECK(!p.Exists("art/new.png.okaymeta") && db.Find(nw)->metaVersion == 0);
        // labels are the other thing that writes a meta
        CHECK(db.AddLabel(nw, "tag") && p.Exists("art/new.png.okaymeta") && db.Find(nw)->HasLabel("TAG"));
    }

    RUN_SUITE("audio files get a waveform thumbnail");
    {
        Project p("wave");
        AudioClip clip = AudioClip::Const(0.8f, 8000, 8000);
        std::error_code mk; fs::create_directories(p.P("audio"), mk);
        CHECK(clip.SaveWAV(p.P("audio/tone.wav").string()));
        p.Text("audio/bad.wav", "not audio");
        AssetDatabase db;
        db.Scan(p.Root());
        ThumbnailCache cache;
        const auto t = cache.Get(db, db.PathToGuid("audio/tone.wav"));
        CHECK(!t.placeholder && t.image->Width() == 128 && t.source == ThumbnailCache::Source::Generated);
        CHECK(t.image->GetPixel(64, 30).g > 0.5f);                                     // the wave fills the column
        const auto bad = cache.Get(db, db.PathToGuid("audio/bad.wav"));
        CHECK(bad.placeholder && bad.failed);
    }

    TEST_MAIN_RESULT();
}
