// Safe scene loading: a bad document never damages the live scene, a cut-short
// file is an error rather than a partial load, counts read from the file are
// bounded, records a newer build wrote are skipped with a warning, and the
// `okayscene` version is checked.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

// A live scene with several object kinds, so "left untouched" means something.
static void Populate(Scene& s) {
    s.SetName("Live");
    GameObject* a = s.CreateGameObject("Alpha");
    a->AddComponent<SpriteRenderer>()->color = Color{0.1f, 0.2f, 0.3f, 1.0f};
    a->tag = "Keep";
    GameObject* b = s.CreateGameObject("Beta");
    b->AddComponent<Camera>();
    b->transform->SetParent(a->transform, false);
    b->prefabSource = "prefabs/beta.okayprefab";
    GameObject* g = s.CreateGameObject("Geo");
    auto* mr = g->AddComponent<MeshRenderer>();
    mr->mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mr->mesh.triangles = {0, 1, 2};
    mr->mesh.uvs = {{0, 0}, {1, 0}, {0, 1}};
    GameObject* d = s.CreateGameObject("Dolly");
    d->AddComponent<DollyPath>()->waypoints = {{0, 0, 0}, {1, 2, 3}, {4, 5, 6}};
    GameObject* tl = s.CreateGameObject("Tiles");
    auto* tm = tl->AddComponent<Tilemap>();
    tm->Resize(3, 2); tm->SetTile(1, 1, 7);
    GameObject* te = s.CreateGameObject("Terrain");
    te->AddComponent<Terrain>()->Resize(8);
    GameObject* ik = s.CreateGameObject("Chain");
    ik->AddComponent<ChainIK>();
}

static std::string LastToken(const std::string& t) {
    std::size_t e = t.find_last_not_of(" \t\r\n");
    if (e == std::string::npos) return "";
    std::size_t b = t.find_last_of(" \t\r\n", e);
    return t.substr(b == std::string::npos ? 0 : b + 1, e - (b == std::string::npos ? 0 : b));
}

static std::string Obj(const std::string& body, int version = 3) {
    return "okayscene " + std::to_string(version) +
           "\ngameobject 0 \"O\"\n  active 1\n  transform 0 0 0 0 0 0 1 1 1 1\n" + body + "\n  parent -1\nend\n";
}

static bool Contains(const std::vector<std::string>& v, const std::string& needle) {
    for (const auto& w : v) if (w.find(needle) != std::string::npos) return true;
    return false;
}

// Loading `text` fails, with an error, and `live` still serializes as before.
static bool FailsClean(Scene& live, const std::string& text, const char* what) {
    const std::string before = SceneSerializer::Serialize(live);
    const std::size_t n = live.Objects().size();
    std::string err;
    const bool ok = SceneSerializer::Deserialize(live, text, &err);
    const bool intact = SceneSerializer::Serialize(live) == before && live.Objects().size() == n && live.Name() == "Live";
    if (ok || err.empty() || !intact)
        std::cerr << "  (" << what << ": ok=" << ok << " err='" << err << "' intact=" << intact << ")\n";
    return !ok && !err.empty() && intact;
}

int main() {
    RUN_SUITE("scene_safe_load");

    Scene live("Live");
    Populate(live);
    const std::string good = SceneSerializer::Serialize(live);

    // ---- documents with no usable header leave the live scene alone ----
    CHECK(FailsClean(live, "this is not a scene at all\n\x01\x02 garbage", "garbage"));
    CHECK(FailsClean(live, "", "empty"));
    CHECK(FailsClean(live, "   \n\n", "blank"));
    CHECK(FailsClean(live, "okayscene", "header without version"));
    CHECK(FailsClean(live, "okayscene banana\nname \"x\"\n", "garbage version"));
    CHECK(FailsClean(live, "okayscene 0\nname \"x\"\n", "version 0"));
    CHECK(FailsClean(live, "okayscene -2\nname \"x\"\n", "negative version"));

    // Header-only is a valid, empty scene (what an empty file of that kind means).
    {
        Scene s("h");
        s.CreateGameObject("Old");
        std::string err;
        CHECK(SceneSerializer::Deserialize(s, "okayscene 3\n", &err));
        CHECK(s.Objects().empty());
    }

    // ---- truncation ----
    {
        // Ten cuts spread over the text.
        for (int i = 1; i <= 10; ++i) {
            const std::string cut = good.substr(0, good.size() * i / 10);
            std::string err;
            Scene probe("Live");
            Populate(probe);
            const std::string before = SceneSerializer::Serialize(probe);
            const bool ok = SceneSerializer::Deserialize(probe, cut, &err);
            if (ok) {
                // Only a cut that lands right after a record's `end` may load.
                CHECK(LastToken(cut) == "end");
            } else {
                CHECK(!err.empty());
                CHECK(SceneSerializer::Serialize(probe) == before);
            }
        }
        // And a sweep through every object: any cut inside a record is an error.
        const std::size_t firstObj = good.find("gameobject");
        int cuts = 0, loaded = 0;
        for (std::size_t at = firstObj + 1; at < good.size(); at += 23) {
            const std::string cut = good.substr(0, at);
            Scene probe("Live");
            Populate(probe);
            const std::string before = SceneSerializer::Serialize(probe);
            std::string err;
            std::vector<std::string> warns;
            const bool ok = SceneSerializer::Deserialize(probe, cut, &err, &warns);
            ++cuts;
            if (ok) {
                ++loaded;
                // A boundary cut, or a clipped record name skipped as unknown.
                CHECK(LastToken(cut) == "end" || !warns.empty());
            } else {
                CHECK(SceneSerializer::Serialize(probe) == before);
            }
        }
        CHECK(cuts > 20);
        CHECK(loaded * 4 < cuts);
        // The whole text still loads and round-trips.
        Scene back("x");
        CHECK(SceneSerializer::Deserialize(back, good));
        CHECK(SceneSerializer::Serialize(back) == good);
    }

    // ---- counts read from the file ----
    {
        const std::string head = "  chainik \"\" 1 10 0 0 0 0 0 0\n";
        CHECK(FailsClean(live, Obj(head + "  chainx2 0 0 0 0 99999999999"), "chainx2 huge count"));
        CHECK(FailsClean(live, Obj(head + "  chainx2 0 0 0 0 4000000000000000000"), "chainx2 enormous count"));
        CHECK(FailsClean(live, Obj(head + "  chainx2 0 0 0 0 -1"), "chainx2 negative count"));
        CHECK(FailsClean(live, Obj(head + "  chainx2 0 0 0 0 -9223372036854775807"), "chainx2 very negative"));
        Scene s("c");
        std::string err;
        CHECK(SceneSerializer::Deserialize(s, Obj(head + "  chainx2 0 0 0 0 0"), &err));
        GameObject* o = s.Find("O");
        CHECK(o && o->GetComponent<ChainIK>() && o->GetComponent<ChainIK>()->limits.empty());
    }

    // ---- forward compatibility ----
    {
        Scene s("f");
        std::vector<std::string> warns;
        std::string err;
        const std::string text = "okayscene 3\nname \"F\"\nfuturerecord 1 2 3\n" +
                                 Obj("  futurefield 9 \"x y\" 3.5\n  tag \"T\"").substr(std::string("okayscene 3\n").size());
        CHECK(SceneSerializer::Deserialize(s, text, &err, &warns));
        CHECK(err.empty());
        CHECK(Contains(warns, "futurerecord"));
        CHECK(Contains(warns, "futurefield"));
        CHECK(warns.size() == 2);
        GameObject* o = s.Find("O");
        CHECK(o && o->tag == "T");               // the line after the skipped one still applied
        CHECK(s.Name() == "F");
    }
    // Version 99: loads, with a warning; the current version warns about nothing.
    {
        Scene s("v");
        std::vector<std::string> warns;
        CHECK(SceneSerializer::Deserialize(s, Obj("  tag \"N\"", 99), nullptr, &warns));
        CHECK(Contains(warns, "99"));
        CHECK(s.Find("O") != nullptr);
        warns.clear();
        Scene s2("v2");
        CHECK(SceneSerializer::Deserialize(s2, good, nullptr, &warns));
        CHECK(warns.empty());
        CHECK(SceneSerializer::kSceneFormatVersion == 3);
    }
    // A newer file whose record this build does read wrongly is still caught.
    CHECK(FailsClean(live, Obj("  transform 0 0", 99), "version 99, cut-short known record"));

    // ---- duplicate indices ----
    CHECK(FailsClean(live, "okayscene 3\ngameobject 4 \"A\"\n  parent -1\nend\ngameobject 4 \"B\"\n  parent -1\nend\n",
                     "duplicate gameobject index"));

    // ---- instantiate ----
    {
        Scene s("i");
        Populate(s);
        s.SetName("Live");
        const std::size_t n = s.Objects().size();
        const std::string before = SceneSerializer::Serialize(s);
        std::string err;
        CHECK(SceneSerializer::InstantiateFromText(s, "garbage", &err) == nullptr);
        CHECK(!err.empty());
        err.clear();
        // Two objects, the second cut short: nothing from the first may remain.
        const std::string cut = "okayscene 3\ngameobject 0 \"P\"\n  parent -1\nend\ngameobject 1 \"Q\"\n  parent 0\n";
        CHECK(SceneSerializer::InstantiateFromText(s, cut, &err) == nullptr);
        CHECK(!err.empty());
        CHECK(s.Objects().size() == n);
        CHECK(SceneSerializer::Serialize(s) == before);
        CHECK(SceneSerializer::MergeFromText(s, cut) == false);
        CHECK(s.Objects().size() == n);

        // The merge path still works, and a clone keeps a nested prefab link.
        GameObject* root = s.Find("Alpha");
        GameObject* clone = SceneSerializer::Instantiate(s, *root);
        CHECK(clone != nullptr);
        CHECK(s.Objects().size() == n + 2);
        CHECK(clone && clone->transform->Children().size() == 1);
        if (clone && clone->transform->Children().size() == 1)
            CHECK(clone->transform->Children()[0]->gameObject->prefabSource == "prefabs/beta.okayprefab");
        CHECK(SceneSerializer::SerializeObject(*root).find("prefab \"prefabs/beta.okayprefab\"") != std::string::npos);
    }

    // ---- appended loads parse once and roll back on failure ----
    {
        Scene s("a");
        GameObject* host = s.CreateGameObject("Host");
        GameObject* kid = s.CreateGameObject("Kid");
        kid->transform->SetParent(host->transform, false);
        // 50 objects in a chain of small subtrees.
        Scene src("src");
        GameObject* prev = nullptr;
        for (int i = 0; i < 50; ++i) {
            GameObject* g = src.CreateGameObject("N" + std::to_string(i));
            g->AddComponent<SpriteRenderer>();
            if (prev && i % 5) g->transform->SetParent(prev->transform, false);
            prev = g;
        }
        const std::string text = SceneSerializer::Serialize(src);
        const std::size_t n = s.Objects().size();
        const std::string before = SceneSerializer::Serialize(s);

        // Cut at several points: nothing added, hierarchy of existing objects unchanged.
        for (int i = 1; i <= 9; ++i) {
            const std::string cut = text.substr(0, text.size() * i / 10);
            std::string err;
            if (LastToken(cut) == "end") continue;   // a clean boundary may load
            CHECK(SceneSerializer::InstantiateFromText(s, cut, &err) == nullptr);
            CHECK(!err.empty());
            CHECK(!SceneSerializer::MergeFromText(s, cut));
            CHECK(s.Objects().size() == n);
            CHECK(host->transform->Children().size() == 1);
            CHECK(kid->transform->Parent() == host->transform);
            CHECK(SceneSerializer::Serialize(s) == before);
        }
        // A cycle found only after every object exists (the last step of a parse).
        const std::string cyc = "okayscene 3\ngameobject 0 \"A\"\n  parent 1\nend\ngameobject 1 \"B\"\n  parent 0\nend\n";
        CHECK(!SceneSerializer::MergeFromText(s, cyc));
        CHECK(s.Objects().size() == n);
        CHECK(host->transform->Children().size() == 1);

        // Success still works, and warnings come back.
        std::vector<GameObject*> roots;
        std::vector<std::string> warns;
        CHECK(SceneSerializer::MergeFromText(s, text.substr(0, text.find('\n') + 1) + "futuretoken 1\n" + text.substr(text.find('\n') + 1), Vec3{1, 0, 0}, &roots, nullptr, &warns));
        CHECK(s.Objects().size() == n + 50);
        CHECK(roots.size() == 10);
        CHECK(Contains(warns, "futuretoken"));
        CHECK(SceneSerializer::InstantiateFromText(s, text) != nullptr);
        CHECK(s.Objects().size() == n + 100);

        // Timing (printed, not asserted): one parse per Instantiate, against the
        // old two (a throwaway parse first, then the real one).
        using Clock = std::chrono::steady_clock;
        auto run = [&](bool probeFirst) {
            Scene t("t");
            const auto t0 = Clock::now();
            for (int i = 0; i < 2000; ++i) {
                if (probeFirst) { Scene p("p"); SceneSerializer::InstantiateFromText(p, text); }
                SceneSerializer::InstantiateFromText(t, text);
                t.Clear();
            }
            return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        };
        const double once = run(false), twice = run(true);
        std::cout << "  2000 x 50-object Instantiate: parse once " << once << " ms, parse twice " << twice << " ms\n";
    }

    // ---- every scene file in the repo loads and round-trips ----
    {
        const char* files[] = {"/tests/export/vs_export.okayscene"};
        for (const char* rel : files) {
            const std::string path = std::string(OKAY_SOURCE_DIR) + rel;
            Scene s("f");
            std::string err;
            std::vector<std::string> warns;
            const bool ok = SceneSerializer::LoadFromFile(s, path, &err, &warns);
            if (!ok) std::cerr << "  (" << path << ": " << err << ")\n";
            CHECK(ok);
            CHECK(warns.empty());
            CHECK(!s.Objects().empty());
            const std::string again = SceneSerializer::Serialize(s);
            Scene s2("g");
            CHECK(SceneSerializer::Deserialize(s2, again, &err));
            // (Not byte equality: a mesh the file names is found next to the scene
            // on the first load only.)
            CHECK(s2.Objects().size() == s.Objects().size());
        }
    }

    TEST_MAIN_RESULT();
}
