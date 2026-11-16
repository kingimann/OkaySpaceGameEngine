// Save-game loads must not stack up the helper objects that components make for themselves
// (issue #888, Finding 54 and its two addenda). Before the fix, every SaveGame::Save -> LoadNow added:
//   - one more set of "TileCollider" boxes (TilemapCollider2D), which also stayed behind when the tilemap moved;
//   - one more "Music A" / "Music B" pair (MusicPlayer);
//   - one more build-mode ghost ("BlockPreview", "BuildPreview", "StructurePreview").
// The helpers are now runtime-only (`transient`, never saved), the tile boxes are children of the tilemap,
// and a component that finds leftovers from an older save removes them.
// The last part runs the same save/load loop over every component type in save_component_list.hpp.
#include "test_framework.hpp"
#include "save_component_list.hpp"

#include <filesystem>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

struct Counts {
    int objects = 0, components = 0;
    bool operator==(const Counts& o) const { return objects == o.objects && components == o.components; }
};

Counts Count(const Scene& s) {
    Counts c;
    for (const auto& up : s.Objects()) {
        if (!up || up->IsDestroyed()) continue;
        ++c.objects;
        c.components += (int)up->Components().size();
    }
    return c;
}

int CountNamed(const Scene& s, const std::string& name) {
    int n = 0;
    for (const auto& up : s.Objects())
        if (up && !up->IsDestroyed() && up->name == name) ++n;
    return n;
}

void Frames(Scene& s, int n) { for (int i = 0; i < n; ++i) s.Update(1.0f / 60.0f); }

// The scene the finding used: a player, a main camera, and a "Host" object that the setup fills.
void BaseScene(Scene& s) {
    GameObject* player = s.CreateGameObject("Player");
    player->tag = "Player";
    GameObject* cam = s.CreateGameObject("Main Camera");
    cam->transform->localPosition = {0.0f, 1.6f, 0.0f};
    s.mainCamera = cam->AddComponent<Camera>();
}

std::string Describe(const std::vector<Counts>& v) {
    std::ostringstream o;
    for (std::size_t i = 0; i < v.size(); ++i) {
        o << (i == 0 ? "start " : " -> load ") ;
        if (i) o << i << " ";
        o << v[i].objects << "/" << v[i].components;
    }
    return o.str();
}

// Start, 3 updates, then Save -> LoadNow -> 3 updates `loads` times. Returns the counts after Start and after
// each load. `check` (optional) runs on the scene after each load.
std::vector<Counts> Cycle(const std::function<void(Scene&, GameObject*)>& setup, int loads,
                          const std::function<void(Scene&)>& check = nullptr) {
    std::vector<Counts> out;
    Scene s("Leak");
    BaseScene(s);
    setup(s, s.CreateGameObject("Host"));
    s.Start();
    Frames(s, 3);
    out.push_back(Count(s));
    for (int i = 0; i < loads; ++i) {
        std::string err;
        if (!SaveGame::Save(s, "leak", "", &err)) { std::cerr << "    save failed: " << err << "\n"; break; }
        if (!SaveGame::LoadNow(s, "leak", &err)) { std::cerr << "    load failed: " << err << "\n"; break; }
        Frames(s, 3);
        out.push_back(Count(s));
        if (check) check(s);
    }
    return out;
}

bool Flat(const std::vector<Counts>& v, std::size_t from = 0) {
    for (std::size_t i = from + 1; i < v.size(); ++i) if (!(v[i] == v[from])) return false;
    return v.size() > from + 1;
}

std::string WriteTone(const fs::path& dir) {
    const std::string path = (dir / "tone.wav").string();
    AudioClip::Const(0.5f, 20000, 1000).SaveWAV(path);   // 20 s at 1000 Hz: still playing after the loads
    return path;
}

GameObject* Find(Scene& s, const std::string& name) { return s.Find(name); }

}  // namespace

int main() {
    RUN_SUITE("save_helper_leaks");
    Log::console = false;
    const fs::path dir = fs::temp_directory_path() / "okay_test_save_helper_leaks";
    fs::remove_all(dir);
    fs::create_directories(dir);
    Save::SetBaseDir(dir.string());
    AudioMixer::sampleRate = 1000;
    const std::string tone = WriteTone(dir);

    // ---- The five components: object and component counts stay flat over three loads ----
    struct Case { const char* name; std::function<void(Scene&, GameObject*)> setup; const char* helper; };
    const std::vector<Case> cases = {
        {"TilemapCollider2D", [](Scene&, GameObject* h) {
             auto* tm = h->AddComponent<Tilemap>();
             tm->tileSize = 1.0f;
             tm->Resize(10, 1);
             for (int x = 0; x < 10; ++x) tm->SetTile(x, 0, 1);
             h->AddComponent<TilemapCollider2D>();
         }, "TileCollider"},
        {"MusicPlayer", [&tone](Scene&, GameObject* h) {
             h->AddComponent<MusicPlayer>()->tracks = {tone};
         }, "Music A"},
        {"BlockBuilder", [](Scene&, GameObject* h) { h->AddComponent<BlockBuilder>(); }, "BlockPreview"},
        {"BuilderMode", [](Scene&, GameObject* h) { h->AddComponent<BuilderMode>(); }, "BuildPreview"},
        {"StructureBuilder", [](Scene&, GameObject* h) { h->AddComponent<StructureBuilder>(); }, "StructurePreview"},
    };
    for (const Case& c : cases) {
        int helpers = -1;
        bool helpersFlat = true;
        const auto v = Cycle(c.setup, 3, [&](Scene& s) {
            const int n = CountNamed(s, c.helper);
            if (helpers < 0) helpers = n; else if (n != helpers) helpersFlat = false;
        });
        std::cout << "  " << c.name << " objects/components: " << Describe(v) << "  (" << c.helper << " x" << helpers << ")\n";
        CHECK(v.size() == 4);
        CHECK(Flat(v));
        CHECK(helpers == 1);
        CHECK(helpersFlat);
    }

    // ---- The music still plays after a load, from one pair of voices ----
    {
        bool playing = true;
        Cycle([&tone](Scene&, GameObject* h) { h->AddComponent<MusicPlayer>()->tracks = {tone}; }, 2,
              [&](Scene& s) {
                  GameObject* host = Find(s, "Host");
                  auto* mp = host ? host->GetComponent<MusicPlayer>() : nullptr;
                  if (!mp || !mp->IsPlaying()) playing = false;
                  if (!host || host->transform->ChildCount() != 2) playing = false;
                  int sounding = 0;
                  for (const auto& up : s.Objects())
                      if (auto* a = up->GetComponent<AudioSource>()) if (a->IsPlaying()) ++sounding;
                  if (sounding != 1) playing = false;   // the real music only, no leftover voice over it
              });
        std::cout << "  music still plays after a load: " << (playing ? "yes" : "no") << "\n";
        CHECK(playing);
    }

    // ---- The build ghost still shows after a load ----
    for (const char* ghost : {"BlockPreview", "BuildPreview", "StructurePreview"}) {
        bool shows = true;
        Cycle([ghost](Scene& s, GameObject* h) {
                  // Something to aim at: the camera looks straight down (cameras look down their -Z, so
                  // Forward points up) at a wide floor. StructureBuilder hides its ghost with nothing to aim at.
                  s.mainCamera->gameObject->transform->LookDirection(Vec3{0.0f, 1.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f});
                  GameObject* floor = s.CreateGameObject("Floor");
                  floor->transform->localPosition = {0.0f, -0.5f, 0.0f};
                  floor->AddComponent<BoxCollider3D>()->size = {40.0f, 1.0f, 40.0f};
                  if (std::string(ghost) == "BlockPreview") h->AddComponent<BlockBuilder>();
                  else if (std::string(ghost) == "BuildPreview") h->AddComponent<BuilderMode>();
                  else h->AddComponent<StructureBuilder>();
              }, 2,
              [&](Scene& s) {
                  GameObject* g = Find(s, ghost);
                  if (!g || !g->ActiveInHierarchy() || !g->GetComponent<MeshRenderer>()) shows = false;
              });
        std::cout << "  " << ghost << " shows after a load: " << (shows ? "yes" : "no") << "\n";
        CHECK(shows);
    }

    // ---- A moved tilemap's colliders follow it; an unmoved one collides exactly as before ----
    {
        Scene s("Move");
        GameObject* ground = s.CreateGameObject("Ground");
        auto* tm = ground->AddComponent<Tilemap>();
        tm->tileSize = 1.0f;
        tm->Resize(10, 1);
        for (int x = 0; x < 10; ++x) tm->SetTile(x, 0, 1);
        ground->AddComponent<TilemapCollider2D>();
        GameObject* body = s.CreateGameObject("Body");
        body->transform->localPosition = {5, 3, 0};
        auto* rb = body->AddComponent<Rigidbody2D>();
        rb->bodyType = Rigidbody2D::BodyType::Dynamic;
        body->AddComponent<BoxCollider2D>()->size = {1, 1};
        s.Start();
        ground->transform->SetPosition({0.0f, -20.0f, 0.0f});   // moved after Start (spawned, then placed)
        Frames(s, 240);
        const float y = body->transform->Position().y;
        std::cout << "  body over a tilemap moved to y=-20 rests at y=" << y << " (the old spot is 1.5)\n";
        CHECK(y < -15.0f);
        CHECK(y > -19.0f);   // it lands on the moved row (top at -19), at about -18.5
    }
    {
        // Built at an offset under a scaled tilemap: the boxes sit where they always did, unscaled.
        Scene s("Place");
        GameObject* ground = s.CreateGameObject("Ground");
        ground->transform->localPosition = {3.0f, 4.0f, 0.0f};
        ground->transform->localScale = {2.0f, 2.0f, 1.0f};
        auto* tm = ground->AddComponent<Tilemap>();
        tm->tileSize = 1.0f;
        tm->Resize(10, 2);
        for (int x = 0; x < 10; ++x) tm->SetTile(x, 1, 1);
        tm->SetTile(2, 0, 1);
        auto* tc = ground->AddComponent<TilemapCollider2D>();
        s.Start();
        CHECK(tc->ColliderCount() == 2);
        int seen = 0;
        for (const auto& up : s.Objects()) {
            if (up->name != "TileCollider") continue;
            auto* bc = up->GetComponent<BoxCollider2D>();
            CHECK(bc != nullptr);
            CHECK(up->transient);
            CHECK(up->transform->Parent() == ground->transform);
            if (!bc) continue;
            const Vec2 c = bc->WorldCenter(), h = bc->HalfExtents();
            if (h.x > 1.0f) {   // the row of 10
                CHECK_NEAR(c.x, 8.0f, 1e-4); CHECK_NEAR(c.y, 5.5f, 1e-4);
                CHECK_NEAR(h.x, 5.0f, 1e-4); CHECK_NEAR(h.y, 0.5f, 1e-4);
            } else {            // the single tile at (2, 0)
                CHECK_NEAR(c.x, 5.5f, 1e-4); CHECK_NEAR(c.y, 4.5f, 1e-4);
                CHECK_NEAR(h.x, 0.5f, 1e-4); CHECK_NEAR(h.y, 0.5f, 1e-4);
            }
            ++seen;
        }
        CHECK(seen == 2);
        // Build again (after SetTile): the old boxes go, only the new set stays.
        tm->SetTile(2, 0, 0);
        tc->Build();
        Frames(s, 1);
        std::cout << "  rebuild after clearing a tile: " << CountNamed(s, "TileCollider") << " boxes (want 1)\n";
        CHECK(tc->ColliderCount() == 1);
        CHECK(CountNamed(s, "TileCollider") == 1);
    }

    // ---- An old save that already holds the leaked copies is cleaned up on load ----
    {
        Scene old("Old");
        BaseScene(old);
        GameObject* host = old.CreateGameObject("Host");
        auto* tm = host->AddComponent<Tilemap>();
        tm->tileSize = 1.0f;
        tm->Resize(10, 1);
        for (int x = 0; x < 10; ++x) tm->SetTile(x, 0, 1);
        host->AddComponent<TilemapCollider2D>();
        host->AddComponent<MusicPlayer>()->tracks = {tone};
        host->AddComponent<BlockBuilder>();
        // What the old engine wrote after two loads: three root tile boxes, three voice pairs, three ghosts.
        for (int i = 0; i < 3; ++i) {
            GameObject* tc = old.CreateGameObject("TileCollider");
            tc->transform->localPosition = {5.0f, 0.5f, 0.0f};
            tc->AddComponent<BoxCollider2D>()->size = {10.0f, 1.0f};
            for (const char* v : {"Music A", "Music B"}) {
                GameObject* m = old.CreateGameObject(v);
                m->transform->SetParent(host->transform, false);
                m->AddComponent<AudioSource>()->group = AudioGroup::Music;
            }
            GameObject* g = old.CreateGameObject("BlockPreview");
            g->tag = "BlockPreview";
            g->AddComponent<MeshRenderer>();
        }
        // A root "TileCollider" far away belongs to someone else and stays.
        GameObject* other = old.CreateGameObject("TileCollider");
        other->transform->localPosition = {100.0f, 100.0f, 0.0f};
        other->AddComponent<BoxCollider2D>();
        const std::string text = SceneSerializer::Serialize(old);
        Scene s("Loaded");
        std::string err;
        CHECK(SceneSerializer::Deserialize(s, text, &err, nullptr));
        s.Start();
        Frames(s, 3);
        std::cout << "  old save cleaned: TileCollider x" << CountNamed(s, "TileCollider") << " (want 2: 1 + the far one), Music A x"
                  << CountNamed(s, "Music A") << ", Music B x" << CountNamed(s, "Music B") << ", BlockPreview x"
                  << CountNamed(s, "BlockPreview") << "\n";
        CHECK(CountNamed(s, "TileCollider") == 2);
        CHECK(CountNamed(s, "Music A") == 1);
        CHECK(CountNamed(s, "Music B") == 1);
        CHECK(CountNamed(s, "BlockPreview") == 1);
        GameObject* h2 = s.Find("Host");
        auto* mp = h2 ? h2->GetComponent<MusicPlayer>() : nullptr;
        CHECK(mp && mp->IsPlaying());
        // And saving it now writes none of the helpers: loading that again stays the same.
        const Counts before = Count(s);
        CHECK(SaveGame::Save(s, "old", "", &err));
        CHECK(SaveGame::LoadNow(s, "old", &err));
        Frames(s, 3);
        CHECK(Count(s) == before);
    }

    // ---- Every component type: no save/load growth ----
    {
        std::vector<std::string> grows;
        int total = 0;
#define X(T)                                                                                       \
        {                                                                                          \
            ++total;                                                                               \
            const auto v = Cycle([](Scene&, GameObject* h) { h->AddComponent<T>(); }, 3);          \
            if (v.size() != 4) grows.push_back(std::string(#T) + " (save/load failed)");          \
            else if (!Flat(v, 1)) grows.push_back(std::string(#T) + ": " + Describe(v));           \
        }
        OKAY_SAVE_COMPONENTS(X)
#undef X
        std::cout << "  scan: " << total << " component types, " << grows.size() << " grow after a load\n";
        for (const auto& g : grows) std::cout << "    grows: " << g << "\n";
        CHECK(total > 200);
        CHECK(grows.empty());
    }

    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
