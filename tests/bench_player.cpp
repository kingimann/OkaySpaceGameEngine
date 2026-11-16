// Player frame-cost bench. Not a test: steps a scene with real player controllers for a few thousand
// frames and prints microseconds per Scene::Update (median, p95), so the cost of the controller code can be
// read before and after a change, and as the number of players grows.
//   bench_player [--old] [--colliders N] [--frames N] [case ...]
// Cases: fp tp cc npc remote many   (default: all).  --old switches the old per-frame player path on (the
// same switch as OKAY_OLD_PLAYER=1), when that path exists.
#include <Okay.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace okay;
using Mode = PlayerController::ViewMode;
using Clock = std::chrono::steady_clock;

namespace {

double LoadAvg() { std::ifstream f("/proc/loadavg"); double a = 0; f >> a; return a; }

struct Stat { double median = 0, mean = 0, p95 = 0, min = 0; };
Stat Summarise(std::vector<double> v) {
    Stat s; if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.min = v.front(); s.median = v[v.size() / 2]; s.p95 = v[(size_t)(v.size() * 0.95)];
    double sum = 0; for (double x : v) sum += x; s.mean = sum / v.size();
    return s;
}

struct World {
    std::unique_ptr<Scene> sc;
    int colliders = 200;
    explicit World(int nColliders) : colliders(nColliders) {
        sc = std::make_unique<Scene>("bench");
        sc->physicsEnabled = true;
        sc->UseFixedStepPhysics(true);        // as the player and the editor's Play run
        GameObject* f = sc->CreateGameObject("Floor");
        f->transform->localPosition = {0, -0.5f, 0};
        f->AddComponent<BoxCollider3D>()->size = {600, 1, 600};
        // A level's worth of static boxes, spread out, none in the way of the walkers.
        unsigned s = 12345;
        auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (float)((s >> 8) & 0xFFFF) / 65535.0f; };
        for (int i = 0; i < colliders; ++i) {
            GameObject* b = sc->CreateGameObject("Prop");
            b->transform->localPosition = {-250.0f + rnd() * 500.0f, 1.0f, 40.0f + rnd() * 200.0f};
            b->AddComponent<BoxCollider3D>()->size = {2, 2, 2};
        }
    }
};

GameObject* MakePlayer(Scene& sc, Vec3 at, Mode mode, bool camera, bool rig) {
    GameObject* p = sc.CreateGameObject("Player");
    p->transform->localPosition = at;
    if (rig) { p->AddComponent<Character>()->Apply(); HumanoidLibrary::EnsureAnimator(p); }
    p->AddComponent<Rigidbody3D>();
    auto* c = p->AddComponent<BoxCollider3D>();
    c->size = {0.6f, 1.8f, 0.6f}; c->offset = {0, 0.9f, 0};
    PlayerController::Add(p, mode, PlayerView::View::First);
    if (camera) Templates::MountEyeCamera(sc, p, "Main Camera");
    return p;
}

GameObject* MakeCC3D(Scene& sc, Vec3 at, bool rig) {
    GameObject* p = sc.CreateGameObject("Walker");
    p->transform->localPosition = at;
    if (rig) { p->AddComponent<Character>()->Apply(); HumanoidLibrary::EnsureAnimator(p); }
    p->AddComponent<Rigidbody3D>();
    auto* c = p->AddComponent<BoxCollider3D>(); c->size = {0.6f, 1.8f, 0.6f}; c->offset = {0, 0.9f, 0};
    p->AddComponent<CharacterController3D>();
    return p;
}

GameObject* MakeNPC(Scene& sc, Vec3 at, bool rig) {
    GameObject* p = sc.CreateGameObject("NPC");
    p->transform->localPosition = at;
    if (rig) { p->AddComponent<Character>()->Apply(); HumanoidLibrary::EnsureAnimator(p); }
    p->AddComponent<Rigidbody3D>();
    auto* c = p->AddComponent<BoxCollider3D>(); c->size = {0.6f, 1.8f, 0.6f}; c->offset = {0, 0.9f, 0};
    auto* n = p->AddComponent<NPCController>();
    n->behavior = (int)NPCController::Behavior::Wander;
    n->targetName = "Nobody";
    return p;
}

GameObject* MakeRemote(Scene& sc, Vec3 at, Mode mode, bool rig) {
    GameObject* p = MakePlayer(sc, at, mode, false, rig);
    auto* ns = p->AddComponent<NetworkSync>();
    ns->authority = NetworkSync::Authority::Manual;
    ns->owned = false;
    p->AddComponent<NetworkAvatarMotion>();
    return p;
}

struct Result { Stat on, off; double sceneObjects = 0; };

template <class Build>
Result Measure(int colliders, int frames, Build build, bool walk, void (*disable)(GameObject*) = nullptr, bool measureOff = true) {
    Result r;
    for (int pass = 0; pass < (measureOff ? 2 : 1); ++pass) {
        World w(colliders);
        std::vector<GameObject*> actors = build(*w.sc);
        w.sc->Start();
        if (pass == 1)
            for (GameObject* g : actors) {
                for (const auto& c : g->Components()) {
                    Component* comp = c.get();
                    if (dynamic_cast<FirstPersonController*>(comp) || dynamic_cast<ThirdPersonController*>(comp) ||
                        dynamic_cast<CharacterController3D*>(comp) || dynamic_cast<NPCController*>(comp) ||
                        dynamic_cast<PlayerView*>(comp)) comp->enabled = false;
                }
            }
        (void)disable;
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 240; ++i) { Input::FeedKeys(walk ? std::vector<char>{'w'} : std::vector<char>{}); w.sc->Update(dt); }
        std::vector<double> us; us.reserve(frames);
        for (int i = 0; i < frames; ++i) {
            Input::FeedKeys(walk && i < frames - 300 ? std::vector<char>{'w'} : std::vector<char>{});
            const auto t0 = Clock::now();
            w.sc->Update(dt);
            const auto t1 = Clock::now();
            us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        Input::FeedKeys({});
        (pass == 0 ? r.on : r.off) = Summarise(us);
        r.sceneObjects = (double)w.sc->Objects().size();
    }
    return r;
}

void Line(const char* name, int n, int colliders, const Result& r) {
    const double per = n > 0 ? (r.on.median - r.off.median) / n : 0;
    std::printf("%-26s players=%3d colliders=%4d | frame median %8.1f us (p95 %8.1f)  controllers off %8.1f us | controller cost %7.2f us per player\n",
                name, n, colliders, r.on.median, r.on.p95, r.off.median, per);
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv) {
    int colliders = 200, frames = 2000; bool old = false; bool cg = false; int cgN = 1;
    std::vector<std::string> cases;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--old")) old = true;
        else if (!std::strcmp(argv[i], "--colliders") && i + 1 < argc) colliders = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--cg")) cg = true;
        else if (!std::strcmp(argv[i], "--n") && i + 1 < argc) cgN = std::atoi(argv[++i]);
        else cases.emplace_back(argv[i]);
    }
    (void)old;
    // --cg <case> [N]: one pass of the named case with a short run, for valgrind --tool=callgrind
    if (!cases.empty() && cases[0] == "--cg") {}
    auto wanted = [&](const char* c) { if (cases.empty()) return true; for (auto& s : cases) if (s == c) return true; return false; };
    if (cg) {
        const std::string what = cases.empty() ? "fp" : cases[0];
        World w(colliders);
        std::vector<GameObject*> a;
        for (int i = 0; i < cgN; ++i) {
            const Vec3 at{-15.0f + (i % 10) * 3.0f, 0, -(float)(i / 10) * 3.0f};
            if (what == "fp") a.push_back(MakePlayer(*w.sc, at, Mode::First, i == 0, true));
            else if (what == "tp") a.push_back(MakePlayer(*w.sc, at, Mode::Third, i == 0, true));
            else if (what == "both") a.push_back(MakePlayer(*w.sc, at, Mode::Both, i == 0, true));
            else if (what == "cc") a.push_back(MakeCC3D(*w.sc, at, true));
            else if (what == "npc") a.push_back(MakeNPC(*w.sc, at, true));
            else if (what == "remote") a.push_back(MakeRemote(*w.sc, at, Mode::Third, true));
        }
        if (what == "remote") w.sc->CreateGameObject("Net")->AddComponent<NetworkManager>();
        if (what == "remote") Templates::MountEyeCamera(*w.sc, MakePlayer(*w.sc, {100, 0, 100}, Mode::First, false, false), "Main Camera");
        w.sc->Start();
        const bool walk = what != "npc" && what != "remote" && std::getenv("BENCH_IDLE") == nullptr;
        for (int i = 0; i < 150; ++i) { Input::FeedKeys(walk ? std::vector<char>{'w'} : std::vector<char>{}); w.sc->Update(1.0f / 60.0f); }
        for (int i = 0; i < frames; ++i) { Input::FeedKeys(walk ? std::vector<char>{'w'} : std::vector<char>{}); w.sc->Update(1.0f / 60.0f); }
        return 0;
    }
    std::printf("# bench_player: machine load average before: %.2f, colliders %d, frames %d, 60 fps frames, 50 Hz physics + interpolation, -O3\n", LoadAvg(), colliders, frames);

    if (wanted("fp")) {
        Line("1 first-person player", 1, colliders, Measure(colliders, frames, [](Scene& s) { return std::vector<GameObject*>{MakePlayer(s, {0, 0, 0}, Mode::First, true, true)}; }, true));
        Line("1 first-person (idle)", 1, colliders, Measure(colliders, frames, [](Scene& s) { return std::vector<GameObject*>{MakePlayer(s, {0, 0, 0}, Mode::First, true, true)}; }, false));
    }
    if (wanted("tp")) {
        Line("1 third-person player", 1, colliders, Measure(colliders, frames, [](Scene& s) { return std::vector<GameObject*>{MakePlayer(s, {0, 0, 0}, Mode::Third, true, true)}; }, true));
        Line("1 both-views player", 1, colliders, Measure(colliders, frames, [](Scene& s) { return std::vector<GameObject*>{MakePlayer(s, {0, 0, 0}, Mode::Both, true, true)}; }, true));
    }
    if (wanted("cc")) {
        for (int n : {1, 10, 50, 100})
            Line("Character Controller 3D", n, colliders, Measure(colliders, n >= 50 ? frames / 4 : frames, [n](Scene& s) {
                std::vector<GameObject*> v; for (int i = 0; i < n; ++i) v.push_back(MakeCC3D(s, {-15.0f + (i % 10) * 3.0f, 0, -(float)(i / 10) * 3.0f}, true)); return v; }, true));
    }
    if (wanted("npc")) {
        for (int n : {1, 10, 50, 100})
            Line("NPC Controller (wander)", n, colliders, Measure(colliders, n >= 50 ? frames / 4 : frames, [n](Scene& s) {
                std::vector<GameObject*> v; for (int i = 0; i < n; ++i) v.push_back(MakeNPC(s, {-15.0f + (i % 10) * 3.0f, 0, -(float)(i / 10) * 3.0f}, true)); return v; }, false));
    }
    if (wanted("remote")) {
        for (int n : {1, 10, 50, 100}) for (Mode m : {Mode::First, Mode::Third})
            Line(m == Mode::First ? "remote avatars (first)" : "remote avatars (third)", n, colliders, Measure(colliders, n >= 50 ? frames / 4 : frames, [n, m](Scene& s) {
                s.CreateGameObject("Net")->AddComponent<NetworkManager>();   // with a manager, an avatar this peer does not own is a remote proxy
                std::vector<GameObject*> v; for (int i = 0; i < n; ++i) v.push_back(MakeRemote(s, {-15.0f + (i % 10) * 3.0f, 0, -(float)(i / 10) * 3.0f}, m, true));
                Templates::MountEyeCamera(s, MakePlayer(s, {100, 0, 100}, Mode::First, false, false), "Main Camera"); return v; }, false));
    }
    if (wanted("many")) {
        for (int n : {1, 10, 50})
            Line("local first-person x N", n, colliders, Measure(colliders, n >= 50 ? frames / 4 : frames, [n](Scene& s) {
                std::vector<GameObject*> v; for (int i = 0; i < n; ++i) v.push_back(MakePlayer(s, {-15.0f + (i % 10) * 3.0f, 0, -(float)(i / 10) * 3.0f}, Mode::First, i == 0, true)); return v; }, true));
    }
    std::printf("# machine load average after: %.2f\n", LoadAvg());
    return 0;
}
