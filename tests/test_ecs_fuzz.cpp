// Randomised stress test of the entity/component system (docs/ecs_upgrade.md).
// Random create / destroy / reparent / add / remove / enable / disable sequences, including ones done from INSIDE Update
// (a component destroying other objects, adding and removing components of others and of itself), with the invariants
// checked after every frame:
//   - no leaks: every component ever made is freed by the end (a live counter returns to zero);
//   - no dangling: ObjectRef / ComponentRef kept for every object read null once it is gone and the right object while alive;
//   - parent/child links agree both ways, no cycles, ids and guids are unique, every component points at its object;
//   - the per-type lists (FindObjectsOfType, FindObjectOfType) match a plain walk;
//   - the whole run is deterministic: the same seed gives the same log hash twice.
// The seeds are fixed, so a failure repeats. Build with -fsanitize=address,undefined to also catch use-after-free
// (scripts: see docs/ecs_upgrade.md). Written from our own list of invariants.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace okay;

namespace {

struct Lcg {
    std::uint64_t s;
    explicit Lcg(std::uint64_t seed) : s(seed) {}
    std::uint32_t Next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return static_cast<std::uint32_t>(s >> 33); }
    int Below(int n) { return static_cast<int>(Next() % static_cast<std::uint32_t>(n)); }
};

Lcg* g_rng = nullptr;
Scene* g_scene = nullptr;
int g_live = 0;                  // Probe objects alive (constructed - destroyed)
std::uint64_t g_hash = 1469598103934665603ULL;   // order of every lifecycle call, mixed
bool g_inUpdateOps = true;
void Mix(std::uint64_t v) { g_hash = (g_hash ^ v) * 1099511628211ULL; }

// A script that does the nasty things from inside its own Update.
struct Probe : Behaviour {
    Probe() { ++g_live; }
    ~Probe() override { --g_live; }
    void Awake() override { Mix(1); }
    void Start() override { Mix(2); }
    void OnEnable() override { Mix(3); }
    void OnDisable() override { Mix(4); }
    void OnDestroy() override { Mix(5); }
    void Update(float) override {
        for (char ch : gameObject->name) Mix(static_cast<unsigned char>(ch));   // which object ran, in call order (ids differ between runs; names do not)
        if (!g_inUpdateOps || !g_rng) return;
        const auto& objs = g_scene->Objects();
        if (objs.empty()) return;
        GameObject* other = objs[static_cast<std::size_t>(g_rng->Below(static_cast<int>(objs.size())))].get();
        switch (g_rng->Below(12)) {
            case 0: g_scene->Destroy(other); break;                      // queued: freed at the end of the frame
            case 1: if (objs.size() < 400) other->AddComponent<Probe>(); break;                 // may be on a destroyed-this-frame object
            case 2: if (auto* p = other->GetComponent<Probe>()) if (p != this) other->RemoveComponent(p); break;
            case 3: if (objs.size() < 400) g_scene->CreateGameObject("born")->AddComponent<Probe>(); break;
            case 4: other->active = !other->active; break;
            case 5: enabled = !enabled; break;
            case 6: if (gameObject && gameObject->transform && other->transform && other != gameObject)
                        other->transform->SetParent(gameObject->transform, false); break;   // a cycle is refused by the engine
            case 7: if (other->transform) other->transform->SetParent(nullptr, false); break;
            case 8: gameObject->RemoveComponent(this); return;           // remove myself: touch nothing after this
            default: break;
        }
    }
};

struct Other : Behaviour { void Update(float) override { Mix(99); } };

// Every structural invariant. Returns a text for the first one that fails ("" = all hold).
std::string Check(Scene& scene) {
    std::unordered_set<std::uint64_t> ids;
    std::unordered_set<std::string> guids;
    std::unordered_set<const GameObject*> inScene;
    for (const auto& up : scene.Objects()) inScene.insert(up.get());
    for (const auto& up : scene.Objects()) {
        const GameObject* g = up.get();
        if (g->IsDestroyed()) return "a destroyed object is still listed";
        if (!g->transform) return "object without a transform";
        if (g->transform->gameObject != g) return "transform does not point back";
        if (g->Components().empty() || g->Components()[0].get() != static_cast<Component*>(g->transform)) return "transform is not first";
        if (!ids.insert(g->InstanceId()).second) return "duplicate instance id";
        if (!guids.insert(g->Guid()).second) return "duplicate guid";
        if (GameObject::FromInstanceId(g->InstanceId()) != g) return "FromInstanceId disagrees";
        std::unordered_set<std::uint64_t> cids;
        for (const auto& c : g->Components()) {
            if (c->gameObject != g) return "component points at another object";
            if (c->ComponentId() == 0 || !cids.insert(c->ComponentId()).second) return "bad or duplicate component id";
        }
        const Transform* p = g->transform->Parent();
        if (p) {
            if (!p->gameObject || !inScene.count(p->gameObject)) return "parent is not in the scene";
            int n = 0;
            for (const Transform* k : p->Children()) if (k == g->transform) ++n;
            if (n != 1) return "parent does not list the child exactly once";
            int depth = 0;
            for (const Transform* q = p; q; q = q->Parent()) if (++depth > 100000) return "cycle in the hierarchy";
        }
        for (const Transform* k : g->transform->Children()) {
            if (!k || k->Parent() != g->transform) return "child does not point at its parent";
            if (!k->gameObject || !inScene.count(k->gameObject)) return "child is not in the scene";
        }
    }
    // The per-type lists agree with a plain walk.
    std::vector<Probe*> walk;
    for (const auto& up : scene.Objects()) up->CollectComponents<Probe>(walk);
    const std::vector<Probe*> listed = scene.FindObjectsOfType<Probe>();
    if (walk != listed) return "FindObjectsOfType differs from a walk";
    Probe* first = scene.FindObjectOfType<Probe>();
    if (first != (walk.empty() ? nullptr : walk.front())) return "FindObjectOfType differs from a walk";
    return "";
}

struct Result { std::uint64_t hash; int frames; int objectsAtEnd; bool ok; };

Result Run(std::uint64_t seed, int steps, bool nasty) {
    Lcg rng(seed);
    g_rng = &rng;
    g_hash = 1469598103934665603ULL;
    g_inUpdateOps = nasty;
    Result r{0, 0, 0, true};
    {
        Scene scene("fuzz");
        g_scene = &scene;
        struct Tracked { ObjectRef ref; ComponentRef<Probe> comp; GameObject* raw; bool destroyedByMe; };
        std::vector<Tracked> tracked;
        for (int i = 0; i < steps && r.ok; ++i) {
            const int n = static_cast<int>(scene.Objects().size());
            switch (rng.Below(9)) {
                case 0: case 1: {   // create, maybe under a parent
                    GameObject* g = scene.CreateGameObject("o" + std::to_string(i));
                    Probe* p = g->AddComponent<Probe>();
                    if (rng.Below(2)) g->AddComponent<Other>();
                    if (n && rng.Below(2)) g->transform->SetParent(scene.Objects()[static_cast<std::size_t>(rng.Below(n))]->transform, false);
                    tracked.push_back({ObjectRef(g), ComponentRef<Probe>(p), g, false});
                    break;
                }
                case 2: if (n) { GameObject* g = scene.Objects()[static_cast<std::size_t>(rng.Below(n))].get(); scene.Destroy(g); break; } break;
                case 3: if (n) { GameObject* g = scene.Objects()[static_cast<std::size_t>(rng.Below(n))].get(); g->AddComponent<Probe>(); } break;
                case 4: if (n) { GameObject* g = scene.Objects()[static_cast<std::size_t>(rng.Below(n))].get(); if (auto* p = g->GetComponent<Probe>()) g->RemoveComponent(p); } break;
                case 5: if (n > 1) { GameObject* a = scene.Objects()[static_cast<std::size_t>(rng.Below(n))].get(); GameObject* b = scene.Objects()[static_cast<std::size_t>(rng.Below(n))].get(); a->transform->SetParent(a == b ? nullptr : b->transform, rng.Below(2) != 0); } break;
                case 6: if (n) { GameObject* g = scene.Objects()[static_cast<std::size_t>(rng.Below(n))].get(); g->active = !g->active; } break;
                case 7: scene.Update(0.016f); ++r.frames; break;
                default: break;
            }
            if (i % 7 == 0) {   // a frame, then every check
                scene.Update(0.016f); ++r.frames;
                const std::string why = Check(scene);
                if (!why.empty()) { std::fprintf(stderr, "  invariant broken at step %d (seed %llu): %s\n", i, (unsigned long long)seed, why.c_str()); r.ok = false; }
                // Handles: alive objects resolve to themselves, gone ones to null, and never to a different object.
                std::unordered_set<const GameObject*> inScene;
                for (const auto& up : scene.Objects()) inScene.insert(up.get());
                for (const Tracked& t : tracked) {
                    GameObject* got = t.ref.Get();
                    const bool listed = inScene.count(t.raw) != 0;
                    if (listed && got != t.raw) { std::fprintf(stderr, "  live ref lost its object\n"); r.ok = false; }
                    if (!listed && got != nullptr) { std::fprintf(stderr, "  ref of a gone object still resolves\n"); r.ok = false; }
                    if (!listed && t.comp.Get() != nullptr) { std::fprintf(stderr, "  ComponentRef of a gone object still resolves\n"); r.ok = false; }
                    if (t.comp.Get() && t.comp.Get()->gameObject != t.raw) { std::fprintf(stderr, "  ComponentRef resolves to another object's component\n"); r.ok = false; }
                }
            }
        }
        scene.Update(0.016f);
        if (!Check(scene).empty()) r.ok = false;
        r.objectsAtEnd = static_cast<int>(scene.Objects().size());
        g_scene = nullptr;
        scene.Clear();
        if (g_live != 0) { std::fprintf(stderr, "  leak: %d probes alive after Clear\n", g_live); r.ok = false; }
    }
    if (g_live != 0) { std::fprintf(stderr, "  leak: %d probes alive after the scene was freed\n", g_live); r.ok = false; }
    r.hash = g_hash;
    g_rng = nullptr;
    return r;
}

} // namespace

int main() {
    RUN_SUITE("ecs fuzz: structure from outside Update");
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        const Result a = Run(seed, 4000, false);
        std::printf("  seed %llu: %d frames, %d objects at the end\n", (unsigned long long)seed, a.frames, a.objectsAtEnd);
        CHECK(a.ok);
        const Result b = Run(seed, 4000, false);
        CHECK(b.hash == a.hash && b.frames == a.frames && b.objectsAtEnd == a.objectsAtEnd);   // deterministic
    }
    RUN_SUITE("ecs fuzz: scripts that destroy, add, remove and reparent inside Update");
    for (std::uint64_t seed = 100; seed <= 105; ++seed) {
        const Result a = Run(seed, 4000, true);
        std::printf("  seed %llu: %d frames, %d objects at the end\n", (unsigned long long)seed, a.frames, a.objectsAtEnd);
        CHECK(a.ok);
        const Result b = Run(seed, 4000, true);
        CHECK(b.hash == a.hash && b.frames == a.frames && b.objectsAtEnd == a.objectsAtEnd);
    }
    TEST_MAIN_RESULT();
}
