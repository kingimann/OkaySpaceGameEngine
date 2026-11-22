// World Partition (split a scene into streaming cells) and Level Streaming (additive levels without a loading
// screen): feature map 2.6.6 and 27.4.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

namespace {

int CountPrefix(const Scene& s, const std::string& pre) {
    int n = 0;
    for (const auto& up : s.Objects())
        if (up && !up->IsDestroyed() && up->name.compare(0, pre.size(), pre) == 0) ++n;
    return n;
}

GameObject* Box(Scene& s, const std::string& name, Vec3 pos, Vec3 scale = Vec3{1, 1, 1}) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos;
    g->transform->localScale = scale;
    g->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
    return g;
}

/// Run frames until `done` or the limit. Each frame also lasts about a millisecond (a busy wait, no threads here), so
/// reads on the job system's workers finish between frames as they would in a game.
template <class F> int Until(Scene& s, F done, int limit = 600) {
    for (int i = 0; i < limit; ++i) {
        if (done()) return i;
        s.Update(1.0f / 60.0f);
        const auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() < 1.0) {}
    }
    return limit;
}

}  // namespace

int main() {
    RUN_SUITE("world_partition");
    const fs::path root = fs::temp_directory_path() / "okay_world_partition";
    fs::remove_all(root);
    fs::create_directories(root);

    // ---- The plan: where each top-level object goes -------------------------------------------------
    Scene big("Big World");
    GameObject* cam = big.CreateGameObject("Main Camera");
    cam->AddComponent<Camera>();
    big.CreateGameObject("Sun")->AddComponent<Light>();                      // directional by default
    big.CreateGameObject("Game Manager");                                     // draws nothing, no collider
    GameObject* lamp = big.CreateGameObject("Lamp");
    lamp->AddComponent<Light>()->type = Light::Type::Point;                   // a point light streams with... nothing to draw
    GameObject* ground = Box(big, "Ground", Vec3{100, -1, 100}, Vec3{200, 1, 200});   // spans 4 cells
    GameObject* forced = Box(big, "Landmark", Vec3{10, 0, 10});
    forced->AddComponent<PartitionLayer>()->mode = PartitionLayer::Mode::AlwaysLoaded;
    GameObject* hero = Box(big, "Hero", Vec3{60, 0, 60});
    hero->tag = "Player";
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            GameObject* house = Box(big, "House_" + std::to_string(i) + "_" + std::to_string(j),
                                    Vec3{i * 50.0f + 20.0f, 0.0f, j * 50.0f + 25.0f}, Vec3{6, 4, 6});
            GameObject* door = big.CreateGameObject("Door_" + std::to_string(i) + "_" + std::to_string(j));
            door->transform->SetParent(house->transform, false);
            door->transform->localPosition = Vec3{0, 0, 0.5f};
        }
    // A wall drawn by its child: the root itself has no renderer, the bounds come from the child.
    GameObject* wallRoot = big.CreateGameObject("Wall");
    Box(big, "WallMesh", Vec3{170, 0, 30})->transform->SetParent(wallRoot->transform, true);
    // A big object that is forced to stream goes to the cell of its centre.
    GameObject* bridge = Box(big, "Bridge", Vec3{120, 0, 180}, Vec3{120, 1, 4});
    bridge->AddComponent<PartitionLayer>()->mode = PartitionLayer::Mode::Streamed;
    // An invisible blocker with a collider streams by its position.
    GameObject* blocker = big.CreateGameObject("Blocker");
    blocker->transform->localPosition = Vec3{-30, 0, -30};
    blocker->AddComponent<BoxCollider3D>();

    PartitionSettings st;
    st.cellSize = 50.0f;
    st.prefix = "cell";
    {
        PartitionPlan plan = PlanPartition(big, st);
        auto find = [&](const std::string& n) -> const PartitionPlan::Entry* {
            for (const auto& e : plan.entries) if (e.object && e.object->name == n) return &e;
            return nullptr;
        };
        CHECK(plan.entries.size() == 26);    // roots only: camera, sun, manager, lamp, ground, landmark, hero, 16 houses, wall, bridge, blocker
        CHECK(find("Main Camera") && find("Main Camera")->alwaysLoaded && find("Main Camera")->reason == "camera");
        CHECK(find("Sun") && find("Sun")->alwaysLoaded);
        CHECK(find("Game Manager") && find("Game Manager")->alwaysLoaded && find("Game Manager")->reason == "draws nothing");
        CHECK(find("Lamp") && find("Lamp")->alwaysLoaded);   // a point light alone draws nothing
        CHECK(find("Ground") && find("Ground")->alwaysLoaded && find("Ground")->reason.find("spans") == 0);
        CHECK(find("Landmark") && find("Landmark")->alwaysLoaded && find("Landmark")->reason == "Partition Layer");
        CHECK(find("Hero") && find("Hero")->alwaysLoaded && find("Hero")->reason == "Player");
        const auto* h = find("House_2_3");
        CHECK(h && !h->alwaysLoaded && h->cellX == 2 && h->cellZ == 3);
        const auto* w = find("Wall");
        CHECK(w && !w->alwaysLoaded && w->cellX == 3 && w->cellZ == 0);
        const auto* b = find("Bridge");
        CHECK(b && !b->alwaysLoaded && b->cellX == 2 && b->cellZ == 3);
        const auto* bl = find("Blocker");
        CHECK(bl && !bl->alwaysLoaded && bl->cellX == -1 && bl->cellZ == -1);
        CHECK(plan.streamed == 19 && plan.alwaysLoaded == 7);
        CHECK(plan.cellCounts.at({2, 3}) == 2);
        CHECK(plan.minX == -50.0f && plan.maxX == 200.0f);
        // Span rule "bounds centre": the ground goes to a cell too.
        PartitionSettings centre = st;
        centre.spanRule = PartitionSettings::SpanRule::BoundsCentre;
        PartitionPlan p2 = PlanPartition(big, centre);
        CHECK(p2.streamed == 20);
    }

    // ---- Writing the cells and the streaming scene; the source is not changed -------------------------
    const std::string srcText = SceneSerializer::Serialize(big);
    const std::string outScene = (root / "big_world_partitioned.okayscene").string();
    const std::string cells = (root / "big_world_cells").string();
    {
        PartitionResult r = WritePartition(big, st, outScene, cells);
        CHECK(r.ok);
        if (!r.ok) std::printf("  partition error: %s\n", r.error.c_str());
        CHECK(r.cellsWritten == 17 && r.cellsRemoved == 0);   // 16 house cells (the wall and the bridge share two of them) + the blocker's
        CHECK(r.objectsStreamed == 19 && r.objectsAlwaysLoaded == 7);
        CHECK(SceneSerializer::Serialize(big) == srcText);    // non-destructive
        int files = 0;
        for (auto& e : fs::directory_iterator(cells)) if (e.path().extension() == ".okayprefab") ++files;
        CHECK(files == r.cellsWritten);
        CHECK(fs::exists(fs::path(cells) / "cell_2_3.okayprefab") && fs::exists(fs::path(cells) / "cell_-1_-1.okayprefab"));

        Scene out("out");
        CHECK(SceneSerializer::LoadFromFile(out, outScene));
        CHECK(out.Find("House_0_0") == nullptr && out.Find("Door_0_0") == nullptr && out.Find("Wall") == nullptr);
        CHECK(out.Find("Ground") && out.Find("Main Camera") && out.Find("Hero") && out.Find("Landmark"));
        GameObject* so = out.Find("World Streamer");
        auto* ws = so ? so->GetComponent<WorldStreamer>() : nullptr;
        CHECK(ws != nullptr);
        if (ws) {
            CHECK(ws->folder == "big_world_cells/");          // relative to the scene, so the pair can move
            CHECK_NEAR(ws->cellSize, 50.0f, 1e-5f);
            CHECK(ws->loadRadius == 1 && ws->unloadRadius == 2 && ws->prefix == "cell");
        }

        // The streamed scene plays: cells near the camera come in with their objects at the same places.
        cam = out.Find("Main Camera");
        out.mainCamera = cam ? cam->GetComponent<Camera>() : nullptr;
        if (cam) cam->transform->localPosition = Vec3{25, 5, 25};    // cell (0,0)
        out.Start();
        out.Update(0.016f);
        Until(out, [&] { return ws && ws->IsCellLoaded(1, 1); });
        CHECK(ws && ws->IsCellLoaded(0, 0) && ws->IsCellLoaded(1, 1) && ws->IsCellLoaded(-1, -1));
        GameObject* h00 = out.Find("House_0_0");
        CHECK(h00 != nullptr);
        if (h00) {
            CHECK_NEAR(h00->transform->Position().x, 20.0f, 1e-4f);
            CHECK_NEAR(h00->transform->Position().z, 25.0f, 1e-4f);
            CHECK(out.Find("Door_0_0") != nullptr);
        }
        CHECK(out.Find("House_3_3") == nullptr);              // far away: not loaded
    }

    // ---- Running it again updates the cells; a partitioned scene is refused -----------------------------
    {
        GameObject* h = big.Find("House_3_3");
        if (h) h->transform->localPosition = Vec3{-80.0f, 0.0f, 20.0f};      // moved into cell (-2, 0)
        for (int i = 0; i < 4; ++i) if (GameObject* g = big.Find("House_" + std::to_string(i) + "_0")) big.DestroyImmediate(g);
        PartitionResult r = WritePartition(big, st, outScene, cells);
        CHECK(r.ok);
        CHECK(fs::exists(fs::path(cells) / "cell_-2_0.okayprefab"));
        CHECK(!fs::exists(fs::path(cells) / "cell_3_3.okayprefab"));        // its only object moved away
        CHECK(!fs::exists(fs::path(cells) / "cell_0_0.okayprefab"));        // emptied
        CHECK(r.cellsRemoved >= 2);
        std::ofstream(fs::path(cells) / "notes.txt") << "keep me";        // not a cell file: never removed
        WritePartition(big, st, outScene, cells);
        CHECK(fs::exists(fs::path(cells) / "notes.txt"));

        Scene again("again");
        CHECK(SceneSerializer::LoadFromFile(again, outScene));
        PartitionResult refused = WritePartition(again, st, (root / "twice.okayscene").string(), cells);
        CHECK(!refused.ok && refused.error.find("already partitioned") != std::string::npos);
        CHECK(fs::exists(fs::path(cells) / "cell_-2_0.okayprefab"));        // nothing was touched
    }

    // ---- Level streaming: LoadLevelAsync / UnloadLevel with completion events ----------------------------
    const std::string levelPath = (root / "Corridor_B.okayscene").string();
    {
        Scene lvl("Corridor_B");
        for (int i = 0; i < 120; ++i) Box(lvl, "Segment_" + std::to_string(i), Vec3{0, 0, 10.0f + i});
        CHECK(SceneSerializer::SaveToFile(lvl, levelPath));
    }
    {
        Scene s("main");
        GameObject* listener = s.CreateGameObject("Listener");
        auto* vsc = listener->AddComponent<VisualScriptComponent>();
        (void)vsc;
        s.Start();
        LevelStreaming::SetAttachBudgetMs(s, 0.05f);
        bool done = false, okFlag = false;
        const int h = LevelStreaming::LoadLevelAsync(s, levelPath, Vec3{100, 0, 0}, [&](bool ok) { done = true; okFlag = ok; }, listener);
        CHECK(h > 0);
        CHECK(LevelStreaming::IsLevelLoading(s, levelPath) && !LevelStreaming::IsLevelLoaded(s, levelPath));
        // Loading it again while it loads gives the same handle, not a second copy.
        CHECK(LevelStreaming::LoadLevelAsync(s, levelPath) == h);
        int maxPerFrame = 0, last = CountPrefix(s, "Segment_");
        const int frames = Until(s, [&] {
            const int now = CountPrefix(s, "Segment_");
            maxPerFrame = std::max(maxPerFrame, now - last);
            last = now;
            return done;
        });
        CHECK(done && okFlag && frames > 2);
        CHECK(maxPerFrame < 120);                                       // spread over frames
        CHECK(CountPrefix(s, "Segment_") == 120);
        CHECK(LevelStreaming::IsLevelLoaded(s, levelPath));
        GameObject* seg = s.Find("Segment_0");
        CHECK(seg && std::fabs(seg->transform->Position().x - 100.0f) < 1e-4f);
        CHECK(LevelStreaming::Levels(s).size() == 1);
        CHECK(LevelStreaming::LevelRoots(s, levelPath).size() == 120);
        std::printf("  [level] 120 objects streamed in over %d frames, at most %d in one frame\n", frames, maxPerFrame);
        // The manager object is never saved.
        CHECK(SceneSerializer::Serialize(s).find("Level Streaming") == std::string::npos);
        CHECK(LevelStreaming::UnloadLevel(s, levelPath, listener));
        s.Update(0.016f);
        CHECK(CountPrefix(s, "Segment_") == 0 && !LevelStreaming::IsLevelLoaded(s, levelPath));
        CHECK(!LevelStreaming::UnloadLevel(s, levelPath));
        // An unknown level fails at once.
        bool failed = false;
        CHECK(LevelStreaming::LoadLevelAsync(s, "No_Such_Level", Vec3{}, [&](bool ok) { failed = !ok; }) == 0);
        CHECK(failed);
        // A name is also looked for next to the scene (sceneDir) with ".okayscene" added.
        CHECK(LevelStreaming::Resolve("Corridor_B", root.string()) == (root / "Corridor_B.okayscene").generic_string());
    }

    // ---- From a script and a visual script: load_level_async + the LevelLoaded custom event -------------
    {
        SceneManager::ClearScenes();
        SceneManager::AddScene(levelPath);                 // a build-list name, as a game would use it
        Scene s("scripted");
        GameObject* o = s.CreateGameObject("Door");
        auto* vc = o->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vc->LoadFromText("node 1 OnEvent LevelLoaded\nentry OnEvent:LevelLoaded 1\nnode 2 AddVar loaded\ndefault 2 0 1\nexec 1 0 2\n"
                               "node 3 OnEvent LevelUnloaded\nentry OnEvent:LevelUnloaded 3\nnode 4 AddVar unloaded\ndefault 4 0 1\nexec 3 0 4\n", &err));
        auto* sc = o->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "var asked = 0; var got = \"\"; var loadedNow = false; var gone = 0;\n"
            "function start() { asked = load_level_async(\"Corridor_B\", 0, 0, 50); }\n"
            "function OnEvent_LevelLoaded(name) { got = name; loadedNow = is_level_loaded(\"Corridor_B\"); unload_level(\"Corridor_B\"); }\n"
            "function OnEvent_LevelUnloaded(name) { gone = gone + 1; }\n", &err));
        s.Start();
        Until(s, [&] { return sc->VM()->GetGlobal("gone").AsFloat() > 0.5f; });
        CHECK_NEAR(sc->VM()->GetGlobal("asked").AsFloat(), 1.0f, 1e-4f);
        CHECK(sc->VM()->GetGlobal("got").AsString() == "Corridor_B");
        CHECK(sc->VM()->GetGlobal("loadedNow").AsBool());
        CHECK_NEAR(sc->VM()->GetGlobal("gone").AsFloat(), 1.0f, 1e-4f);
        CHECK_NEAR(vc->GetVariable("loaded").AsFloat(), 1.0f, 1e-4f);
        CHECK_NEAR(vc->GetVariable("unloaded").AsFloat(), 1.0f, 1e-4f);
        s.Update(0.016f);
        CHECK(CountPrefix(s, "Segment_") == 0);
        SceneManager::ClearScenes();
    }

    // ---- Level Streaming Volume: enter -> load, leave -> unload (with a margin), keep loaded ---------------
    {
        Scene s("vol");
        GameObject* player = s.CreateGameObject("Player");
        player->transform->localPosition = Vec3{0, 0, -100};
        GameObject* v = s.CreateGameObject("Corridor Trigger");
        v->transform->localPosition = Vec3{0, 0, 0};
        auto* vol = v->AddComponent<LevelStreamingVolume>();
        vol->level = levelPath;
        vol->size = Vec3{10, 10, 10};
        vol->preloadDistance = 20.0f;
        vol->unloadMargin = 10.0f;
        vol->target = "Player";
        int loadedEvents = 0, unloadedEvents = 0;
        vol->onLoaded = [&](bool ok) { if (ok) ++loadedEvents; };
        vol->onUnloaded = [&]() { ++unloadedEvents; };
        s.Start();
        for (int i = 0; i < 5; ++i) s.Update(0.016f);
        CHECK(!vol->IsLoaded() && !vol->IsLoading());
        player->transform->localPosition = Vec3{0, 0, -24};       // 19 from the box: inside the preload distance
        s.Update(0.016f);
        CHECK(vol->IsLoading() || vol->IsLoaded());
        Until(s, [&] { return vol->IsLoaded(); });
        CHECK(vol->IsLoaded() && loadedEvents == 1 && CountPrefix(s, "Segment_") == 120);
        player->transform->localPosition = Vec3{0, 0, -30};       // 25: past preload, inside the margin: stays
        for (int i = 0; i < 5; ++i) s.Update(0.016f);
        CHECK(vol->IsLoaded());
        player->transform->localPosition = Vec3{0, 0, -40};       // 35: past preload + margin: unloads
        s.Update(0.016f); s.Update(0.016f);
        CHECK(!vol->IsLoaded() && unloadedEvents == 1 && CountPrefix(s, "Segment_") == 0);
        // A StreamingSource also loads it; keepLoaded keeps it after the source leaves.
        vol->keepLoaded = true;
        GameObject* buddy = s.CreateGameObject("Buddy");
        buddy->transform->localPosition = Vec3{0, 0, 3};
        buddy->AddComponent<StreamingSource>();
        Until(s, [&] { return vol->IsLoaded(); });
        CHECK(vol->IsLoaded() && vol->loadCount == 2);
        buddy->transform->localPosition = Vec3{0, 0, 500};
        for (int i = 0; i < 5; ++i) s.Update(0.016f);
        CHECK(vol->IsLoaded());
        // Saved and loaded with its settings.
        const std::string text = SceneSerializer::SerializeObject(*v);
        CHECK(text.find("levelstreamingvolume") != std::string::npos);
        Scene b("b");
        GameObject* r = SceneSerializer::InstantiateFromText(b, text);
        auto* v2 = r ? r->GetComponent<LevelStreamingVolume>() : nullptr;
        CHECK(v2 && v2->level == levelPath && v2->keepLoaded && std::fabs(v2->preloadDistance - 20.0f) < 1e-5f &&
              std::fabs(v2->size.x - 10.0f) < 1e-5f && v2->target == "Player");
    }

    fs::remove_all(root);
    TEST_MAIN_RESULT();
}
