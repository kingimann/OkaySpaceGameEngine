// World streaming foundation: chunk format, splitter, stores (docs/foundations/world_streaming.md).
#include "test_framework.hpp"
#include "scene_text_testkit.hpp"
#include "okay/Scene/WorldChunk.hpp"
#include "okay/Scene/StreamingSource.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace okay;
using namespace okay::worldpart;

namespace {

// guid of each top-level object -> its object text: what "the same scene" means for chunks.
std::map<std::string, std::string> Snapshot(const Scene& s, int* objectCount = nullptr) {
    std::map<std::string, std::string> m;
    for (GameObject* r : s.Roots()) m[r->Guid()] = SceneSerializer::SerializeObject(*r);
    if (objectCount) { *objectCount = 0; for (const auto& o : s.Objects()) if (o && !o->IsDestroyed()) ++*objectCount; }
    return m;
}

std::string Reseal(const std::string& cellLine, const std::string& rootsLine, const std::string& payload) {
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", (unsigned long long)Fnv1a64(cellLine + rootsLine + payload));
    return "okaychunk 1\n" + cellLine + rootsLine + "sum " + hex + "\n" + payload;
}

Chunk MakeChunk(Scene& scene, int n) {
    Chunk c;
    c.cell = {3, 0, -2};
    for (int i = 0; i < n; ++i) {
        GameObject* g = scene.CreateGameObject("Obj" + std::to_string(i));
        g->transform->localPosition = {float(i), 0, 1};
        ChunkRoot r;
        std::string err;
        if (!CaptureRoot(*g, r, &err)) { std::cerr << err << "\n"; }
        c.roots.push_back(r);
    }
    return c;
}

} // namespace

int main() {
    RUN_SUITE("world_chunk");
    RegisterStreamingSerializers();

    // ---- encode / decode ----
    {
        Scene s("c");
        Chunk c = MakeChunk(s, 3);
        const std::string text = EncodeChunk(c);
        Chunk d;
        std::string err;
        CHECK(DecodeChunk(text, d, &err));
        CHECK(err.empty());
        CHECK(!d.persistent && d.cell == c.cell && d.roots.size() == 3);
        for (int i = 0; i < 3; ++i) CHECK(d.roots[i].guids == c.roots[i].guids && d.roots[i].text == c.roots[i].text);
        CHECK(EncodeChunk(d) == text);              // deterministic
        CHECK(d.Name() == "cell_3_0_n2");
        Chunk p; p.persistent = true;
        CHECK(DecodeChunk(EncodeChunk(p), d, &err) && d.persistent && d.roots.empty() && d.Name() == "persistent");
        // Negative and edge cells survive.
        Chunk e; e.cell = {-kMaxCellIndex, 0, kMaxCellIndex};
        CHECK(DecodeChunk(EncodeChunk(e), d, &err) && d.cell == e.cell);
    }

    // ---- every single damaged byte is caught; every cut-off is caught ----
    {
        Scene s("c");
        const std::string text = EncodeChunk(MakeChunk(s, 2));
        Chunk d;
        int missed = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            std::string bad = text;
            bad[i] = static_cast<char>(bad[i] ^ 0x01);
            if (DecodeChunk(bad, d)) ++missed;
        }
        CHECK(missed == 0);
        for (std::size_t n = 0; n < text.size(); ++n) CHECK(!DecodeChunk(text.substr(0, n), d));
        CHECK(!DecodeChunk(text + "x", d));
        CHECK(!DecodeChunk(text + "\n", d));
        std::string err;
        std::string bad = text; bad[text.size() - 5] ^= 0x20;
        CHECK(!DecodeChunk(bad, d, &err) && err.find("checksum") != std::string::npos);
        CHECK(!DecodeChunk("okaychunk 2\n", d, &err) && err.find("version") != std::string::npos);
        CHECK(!DecodeChunk("hello", d, &err));
        CHECK(!DecodeChunk("", d, &err));
    }

    // ---- hostile chunks with a VALID checksum (the parser, not the sum, must stop them) ----
    {
        Chunk d;
        const std::string guid = "0123456789abcdef0123456789abcdef";
        auto tryit = [&](const std::string& payload, long long roots = 1, const std::string& cell = "cell 0 0 0\n") {
            return DecodeChunk(Reseal(cell, "roots " + std::to_string(roots) + "\n", payload), d);
        };
        CHECK(tryit("root 1 4\n" + guid + "\nabcd\n"));                                  // control: valid
        CHECK(!tryit("root 1 999999999\n" + guid + "\nabcd\n"));                          // text longer than file
        CHECK(!tryit("root 999999999 4\n" + guid + "\nabcd\n"));                          // guid count larger than file
        CHECK(!tryit("root 1 4\n" + guid + "\nabcd"));                                    // no final line end
        CHECK(!tryit("root 1 4\n" + guid + "\nabcdX"));                                   // text not followed by newline
        CHECK(!tryit("root 1 4\n" + guid + "\nabcd\n", 2));                               // fewer roots than said
        CHECK(!tryit("root 1 4\n" + guid + "\nabcd\n", 0));                               // data after the last root
        CHECK(!tryit("root 1 4\n" + guid + "\nabcd\n", -1));
        CHECK(!tryit("root 1 4\n" + guid + "\nabcd\n", 99999999999LL));
        CHECK(!tryit("root 2 4\n" + guid + "\nabcd\n"));                                  // guid count mismatch
        CHECK(!tryit("root 1 4\nNOT-A-GUID\nabcd\n"));                                    // bad guid
        CHECK(!tryit("root 1 4\n\nabcd\n"));                                              // empty guid
        CHECK(!tryit("root 2 4\n" + guid + " " + guid + "\nabcd\n"));                     // repeated guid
        CHECK(!tryit("root 1 4\n" + guid + "  \nabcd\n"));
        CHECK(!tryit("root 0 4\n\nabcd\n"));                                              // no objects
        CHECK(!tryit("root 1 0\n" + guid + "\n\n"));                                      // empty text
        CHECK(!tryit("root -1 4\n" + guid + "\nabcd\n"));
        CHECK(!tryit("root 1 -4\n" + guid + "\nabcd\n"));
        CHECK(!tryit("root 1 99999999999\n" + guid + "\nabcd\n"));                        // more than 10 digits
        CHECK(!tryit("root 1 4 5\n" + guid + "\nabcd\n"));
        CHECK(!tryit("rootx 1 4\n" + guid + "\nabcd\n"));
        CHECK(!tryit("root 1 4\n" + guid + "\nabcd\n", 1, "cell 1048576 0 0\n"));          // beyond the world
        CHECK(!tryit("root 1 4\n" + guid + "\nabcd\n", 1, "cell 1 2\n"));
        CHECK(!tryit("root 1 4\n" + guid + "\nabcd\n", 1, "cell 1 2 3 4\n"));
        CHECK(!tryit("root 1 4\n" + guid + "\nabcd\n", 1, "cell a 2 3\n"));
        CHECK(!tryit("root 1 4\n" + guid + "\nabcd\n", 1, "cell 99999999999999 2 3\n"));
        CHECK(!tryit("root 1 4\n" + guid + "\nabcd\n", 1, "cell persistentx\n"));
        CHECK(!tryit("root 1 4\n" + guid + "\nabcd\n", 1, "cell\n"));
        CHECK(tryit("root 1 4\n" + guid + "\nabcd\n", 1, "cell persistent\n"));
        // A line that never ends within the limit.
        CHECK(!DecodeChunk("okaychunk 1\n" + std::string(100000, 'a'), d));
        CHECK(!DecodeChunk("okaychunk 1\ncell 0 0 0\nroots 1\nsum " + std::string(5000, 'a'), d));
        // Embedded NULs and high bytes in the text are data, not terminators.
        std::string nul = "ab\0c";
        nul.push_back('\xff');
        CHECK(tryit("root 1 " + std::to_string(nul.size()) + "\n" + guid + "\n" + nul + "\n"));
        CHECK(d.roots[0].text == nul);
    }

    // ---- seeded fuzz: mutate a valid payload, re-seal, decode and apply; nothing may crash ----
    {
        Scene src("src");
        Chunk base = MakeChunk(src, 4);
        const std::string text = EncodeChunk(base);
        const std::size_t payloadAt = text.find("\nsum ") + 1 + text.substr(text.find("\nsum ") + 1).find('\n') + 1;
        const std::string head = text.substr(0, payloadAt);
        const std::string payload = text.substr(payloadAt);
        tk::TkRng rng(0xC0FFEEu);
        int decoded = 0, applied = 0;
        const int rounds = tk::Seeds(1500);
        for (int i = 0; i < rounds; ++i) {
            std::string p = payload;
            const int edits = 1 + rng.range(4);
            for (int e = 0; e < edits && !p.empty(); ++e) {
                const std::size_t at = static_cast<std::size_t>(rng.range(static_cast<int>(p.size())));
                switch (rng.range(5)) {
                    case 0: p[at] = static_cast<char>(rng.range(256)); break;
                    case 1: p.erase(at, 1 + rng.range(8)); break;
                    case 2: p.insert(at, std::string(1 + rng.range(6), static_cast<char>("\n 0-9a\"x"[rng.range(8)]))); break;
                    case 3: { const std::size_t len = 1 + static_cast<std::size_t>(rng.range(40)); p.insert(at, p.substr(at, len)); break; }
                    default: p.resize(at); break;
                }
            }
            Chunk d;
            const std::string cellLine = "cell 3 0 -2\n";
            const std::string sealed = Reseal(cellLine, "roots " + std::to_string(base.roots.size()) + "\n", p);
            if (DecodeChunk(sealed, d)) {
                ++decoded;
                Scene probe("probe");
                std::string err;
                const ChunkApply a = InstantiateChunk(probe, d, nullptr, &err);
                if (a == ChunkApply::Ok) ++applied;
                else CHECK(probe.Objects().empty());       // all or nothing
            }
            // And raw (unsealed) damage: always rejected.
            std::string raw = head + p;
            if (raw != text) { Chunk d2; CHECK(!DecodeChunk(raw, d2)); }
            (void)head;
        }
        CHECK(decoded > 0);        // the fuzz does reach the structure parser
        std::cout << "  fuzz: " << rounds << " rounds, " << decoded << " decoded, " << applied << " applied\n";
        // Pure garbage.
        for (int i = 0; i < 300; ++i) {
            std::string g;
            const int n = rng.range(200);
            for (int k = 0; k < n; ++k) g.push_back(static_cast<char>(rng.range(256)));
            Chunk d;
            CHECK(!DecodeChunk(g, d));
            CHECK(!DecodeChunk("okaychunk 1\n" + g, d));
        }
    }

    // ---- instantiate: guids kept, all or nothing, collisions, busy ----
    {
        Scene a("a");
        GameObject* root = a.CreateGameObject("Root");
        GameObject* kid1 = a.CreateGameObject("Kid1");
        GameObject* kid2 = a.CreateGameObject("Kid2");
        GameObject* grand = a.CreateGameObject("Grand");
        kid1->transform->SetParent(root->transform, false);
        kid2->transform->SetParent(root->transform, false);
        grand->transform->SetParent(kid1->transform, false);
        root->transform->localPosition = {5, 0, 6};
        kid2->tag = "enemy";
        ChunkRoot cr;
        CHECK(CaptureRoot(*root, cr));
        CHECK(cr.guids.size() == 4);
        CHECK(cr.guids[0] == root->Guid() && cr.guids[1] == kid1->Guid() && cr.guids[2] == grand->Guid() && cr.guids[3] == kid2->Guid());
        Chunk c; c.roots.push_back(cr);

        Scene b("b");
        std::vector<std::string> roots;
        std::string err;
        CHECK(InstantiateChunk(b, c, &roots, &err) == ChunkApply::Ok);
        CHECK(roots.size() == 1 && roots[0] == root->Guid());
        CHECK(b.FindByGuid(root->Guid()) && b.FindByGuid(grand->Guid()) && b.FindByGuid(kid2->Guid()));
        CHECK(b.FindByGuid(kid2->Guid())->name == "Kid2" && b.FindByGuid(kid2->Guid())->tag == "enemy");
        CHECK(b.FindByGuid(grand->Guid())->transform->Parent()->gameObject == b.FindByGuid(kid1->Guid()));
        CHECK(b.Objects().size() == 4);
        // Again: guids exist, the scene is not touched.
        CHECK(InstantiateChunk(b, c, nullptr, &err) == ChunkApply::Failed);
        CHECK(err.find("already") != std::string::npos);
        CHECK(b.Objects().size() == 4);
        // Destroy queued but not flushed: Busy, then Ok after the flush.
        b.Destroy(b.FindByGuid(root->Guid()));
        CHECK(InstantiateChunk(b, c, nullptr, &err) == ChunkApply::Busy);
        b.FlushDestroyed();
        CHECK(b.FindByGuid(root->Guid()) == nullptr);
        CHECK(InstantiateChunk(b, c, nullptr, &err) == ChunkApply::Ok);

        // A chunk whose second tree is bad adds nothing at all.
        Chunk bad = c;
        ChunkRoot junk; junk.guids = {"00000000000000000000000000000001"}; junk.text = "this is not an object\n";
        bad.roots.push_back(junk);
        Scene z("z");
        CHECK(InstantiateChunk(z, bad, nullptr, &err) == ChunkApply::Failed);
        CHECK(z.Objects().empty());
        // The chunk lists the wrong number of guids.
        Chunk wrong = c; wrong.roots[0].guids.pop_back();
        CHECK(InstantiateChunk(z, wrong, nullptr, &err) == ChunkApply::Failed);
        CHECK(z.Objects().empty());
    }

    // ---- splitter: scene -> chunks -> scene is the same scene ----
    {
        const int seeds = tk::Seeds(12);
        int totalCells = 0;
        for (int seed = 1; seed <= seeds; ++seed) {
            for (GridPlane plane : {GridPlane::XZ, GridPlane::XY, GridPlane::XYZ}) {
                tk::TkRng rng(static_cast<std::uint64_t>(seed) * 977u + static_cast<std::uint64_t>(plane));
                Scene orig("orig");
                std::string err;
                CHECK(SceneSerializer::Deserialize(orig, tk::LiveSceneText(rng, 40 + rng.range(40)), &err));
                // An always-loaded object (with a child), and a streaming source.
                GameObject* keep = orig.CreateGameObject("Sky");
                keep->tag = kAlwaysLoadedTag;
                keep->transform->localPosition = {500, 0, 500};
                GameObject* keepKid = orig.CreateGameObject("SkyKid");
                keepKid->transform->SetParent(keep->transform, false);
                GameObject* player = orig.CreateGameObject("Player");
                player->transform->localPosition = {-3, 0, 4};
                auto* src = player->AddComponent<StreamingSource>();
                src->loadRadius = 33; src->unloadRadius = 55; src->priority = 2;
                GameObject* far = orig.CreateGameObject("FarAway");
                far->transform->localPosition = {1e30f, 0, 0};     // outside the world
                GameObject* nan = orig.CreateGameObject("NotANumber");
                nan->transform->localPosition = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
                orig.FlushDestroyed();

                WorldGrid grid(8.0f, plane);
                SplitResult split;
                CHECK(SplitScene(orig, grid, SplitOptions{}, split, &err));
                CHECK(split.chunks.count(kPersistentChunkName) == 1);
                CHECK(split.persistentRoots >= 3);
                CHECK(split.cells.size() > 4);
                CHECK(split.cellRoots + split.persistentRoots + split.skippedRoots == static_cast<int>(orig.Roots().size()));
                totalCells += static_cast<int>(split.cells.size());

                // Each cell holds only trees whose origin is in that cell (negative cells included).
                bool negative = false;
                for (const CellCoord& cell : split.cells) {
                    Chunk ch;
                    CHECK(DecodeChunk(split.chunks.at(WorldGrid::CellName(cell)), ch, &err));
                    CHECK(ch.cell == cell && !ch.persistent);
                    if (cell.x < 0 || cell.y < 0 || cell.z < 0) negative = true;
                    Scene tmp("tmp");
                    CHECK(InstantiateChunk(tmp, ch) == ChunkApply::Ok);
                    for (GameObject* r : tmp.Roots()) {
                        CellCoord at;
                        CHECK(grid.CellOf(r->transform->Position(), at) && at == cell);
                    }
                }
                CHECK(negative);

                Scene back("back");
                MemoryChunkStore store;
                store.Add(split);
                for (const std::string& name : store.List()) {
                    std::string text;
                    Chunk ch;
                    CHECK(store.Read(name, text) && DecodeChunk(text, ch, &err));
                    CHECK(InstantiateChunk(back, ch, nullptr, &err) == ChunkApply::Ok);
                }
                int on = 0, bn = 0;
                auto o = Snapshot(orig, &on);
                auto b = Snapshot(back, &bn);
                // The scene reader resets a NaN position to 0 (it warns), so that one object differs by design.
                CHECK(back.Find("NotANumber") && std::isfinite(back.Find("NotANumber")->transform->localPosition.x));
                o.erase(nan->Guid()); b.erase(nan->Guid());
                CHECK(on == bn);
                CHECK(o.size() == b.size());
                CHECK(o == b);
                // Hierarchy, names, parents: every guid maps to an object with the same parent guid.
                for (const auto& go : orig.Objects()) {
                    const GameObject* twin = back.FindByGuid(go->Guid());
                    CHECK(twin != nullptr);
                    if (!twin) continue;
                    CHECK(twin->name == go->name);
                    const Transform* p1 = go->transform->Parent();
                    const Transform* p2 = twin->transform->Parent();
                    CHECK((p1 == nullptr) == (p2 == nullptr));
                    if (p1 && p2) CHECK(p1->gameObject->Guid() == p2->gameObject->Guid());
                }
                // The always-loaded set is exactly the marked objects, the source and the unaddressable ones.
                Chunk pc;
                CHECK(DecodeChunk(split.chunks.at(kPersistentChunkName), pc));
                Scene ps("ps");
                CHECK(InstantiateChunk(ps, pc) == ChunkApply::Ok);
                CHECK(ps.Find("Sky") && ps.Find("SkyKid") && ps.Find("Player") && ps.Find("FarAway") && ps.Find("NotANumber"));
                CHECK(ps.Find("Player")->GetComponent<StreamingSource>() != nullptr);
                CHECK(ps.Find("Player")->GetComponent<StreamingSource>()->loadRadius == 33.0f);
                CHECK(ps.Find("Player")->GetComponent<StreamingSource>()->priority == 2.0f);
                CHECK(split.outsideWorld == 2);
            }
        }
        std::cout << "  splitter: " << totalCells << " cells over " << seeds * 3 << " scenes\n";

        // Splitting is deterministic, and does not change the scene.
        tk::TkRng rng(5);
        Scene s("s");
        std::string err;
        CHECK(SceneSerializer::Deserialize(s, tk::LiveSceneText(rng, 50), &err));
        const std::string before = SceneSerializer::Serialize(s);
        SplitResult a, b;
        WorldGrid grid(8.0f, GridPlane::XY);
        CHECK(SplitScene(s, grid, SplitOptions{}, a) && SplitScene(s, grid, SplitOptions{}, b));
        CHECK(a.chunks == b.chunks);
        CHECK(SceneSerializer::Serialize(s) == before);
        // Invalid grid and empty scene.
        SplitResult r;
        CHECK(!SplitScene(s, WorldGrid(0.0f), SplitOptions{}, r, &err) && !err.empty() && r.chunks.empty());
        Scene empty("e");
        CHECK(SplitScene(empty, grid, SplitOptions{}, r) && r.chunks.empty());
        // A custom tag.
        Scene t("t");
        t.CreateGameObject("A")->tag = "Forever";
        t.CreateGameObject("B");
        SplitOptions opt; opt.alwaysLoadedTag = "Forever";
        CHECK(SplitScene(t, grid, opt, r) && r.persistentRoots == 1 && r.cellRoots == 1);
    }

    // ---- stores ----
    {
        MemoryChunkStore m;
        std::string out;
        CHECK(!m.Read("x", out));
        CHECK(m.Write("b", "2") && m.Write("a", "1"));
        CHECK(m.Read("a", out) && out == "1");
        CHECK((m.List() == std::vector<std::string>{"a", "b"}));

        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "okay_world_chunk_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        DirectoryChunkStore d(dir.string());
        CHECK(d.List().empty());
        CHECK(d.Write("cell_0_0_0", "hello\nworld\0x"));
        std::string big(100000, 'q');
        CHECK(d.Write("cell_n1_0_2", big));
        CHECK(d.Read("cell_n1_0_2", out) && out == big);
        CHECK(d.Write("cell_n1_0_2", "short") && d.Read("cell_n1_0_2", out) && out == "short");
        CHECK(!fs::exists(dir / "cell_n1_0_2.okaychunk.tmp"));
        CHECK((d.List() == std::vector<std::string>{"cell_0_0_0", "cell_n1_0_2"}));
        // Names cannot leave the folder or hold odd characters.
        for (const char* n : {"", "../evil", "a/b", "a\\b", "..", ".", "Cell", "a b", "a.okaychunk"})
            CHECK(!DirectoryChunkStore::ValidName(n));
        CHECK(!DirectoryChunkStore::ValidName(std::string("x\0y", 3)));
        CHECK(!d.Write("../evil", "x") && !d.Read("../evil", out));
        CHECK(!fs::exists(dir.parent_path() / "evil.okaychunk"));
        CHECK(!d.Read("missing", out));
        { std::ofstream(dir / "stray.txt") << "x"; std::ofstream(dir / "BAD NAME.okaychunk") << "x"; }
        CHECK((d.List() == std::vector<std::string>{"cell_0_0_0", "cell_n1_0_2"}));
        // Write into an unwritable place fails cleanly.
        DirectoryChunkStore nowhere((dir / "cell_0_0_0.okaychunk" / "sub").string());   // a file is in the way
        CHECK(!nowhere.Write("a", "x"));
        fs::remove_all(dir, ec);
    }

    TEST_MAIN_RESULT();
}
