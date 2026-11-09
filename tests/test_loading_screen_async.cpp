// Loading Screen (12.8) with a background load (1.4): the screen stays up while the target scene is made over
// several frames, the bar follows the real progress, the screen's object goes when it is done, a failed load
// reveals the scene that was showing, and scenes saved before background loading keep loading in one go.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

std::string Dir() {
    static const std::string d = (fs::current_path() / "loading_async_tmp").string();
    fs::create_directories(d);
    return d;
}

// A level of `n` objects "L0".."L<n-1>".
std::string WriteLevel(const std::string& file, int n) {
    Scene sc("level");
    for (int i = 0; i < n; ++i) sc.CreateGameObject("L" + std::to_string(i))->AddComponent<SpriteRenderer>();
    const std::string path = Dir() + "/" + file;
    SceneSerializer::SaveToFile(sc, path);
    return path;
}

// The transition scene: an object "Old" and the loading screen on an object of its own.
std::string WriteTransition(const std::string& file, const std::string& target, bool alone = true, bool async = true) {
    Scene sc("transition");
    sc.CreateGameObject("Old")->AddComponent<SpriteRenderer>();
    GameObject* g = sc.CreateGameObject("Loading");
    auto* ls = g->AddComponent<LoadingScreen>();
    ls->targetScene = target;
    ls->duration = 0.5f;
    ls->fadeTime = 0.2f;
    ls->objectsPerFrame = 2;
    ls->asyncLoad = async;
    if (!alone) g->AddComponent<Mover>();
    const std::string path = Dir() + "/" + file;
    SceneSerializer::SaveToFile(sc, path);
    return path;
}

std::vector<std::string> Names(const Scene& s) {
    std::vector<std::string> n;
    for (const auto& o : s.Objects()) n.push_back(o->name);
    return n;
}
LoadingScreen* Screen(Scene& s) {
    GameObject* g = s.Find("Loading");
    return g ? g->GetComponent<LoadingScreen>() : nullptr;
}

void TestBackgroundLoad() {
    RUN_SUITE("a background load: the screen covers the build, then goes");
    const std::string level = WriteLevel("level.okayscene", 11);
    const std::string trans = WriteTransition("trans.okayscene", level);
    Scene s("game");
    CHECK(SceneSerializer::LoadFromFile(s, trans));
    s.Start();
    LoadingScreen* ls = Screen(s);
    CHECK(ls && ls->Active() && ls->asyncLoad);
    if (!ls) return;
    CHECK(ls->Loading());
    GameObject* screenObject = ls->gameObject;
    CHECK(s.IsDontDestroyOnLoad(screenObject));

    float last = 0.0f;
    bool sawPartial = false, sawLoaded = false;
    for (int frame = 0; frame < 40; ++frame) {
        s.Update(0.1f);
        LoadingScreen* now = Screen(s);
        if (!now) break;
        CHECK(now == ls);                              // the same screen all through (it was kept across the swap)
        const float p = now->Progress();
        CHECK(p >= last - 1e-6f && p <= 1.0f);
        last = p;
        CHECK(now->Alpha() > 0.0f || !now->Active());
        const std::size_t objects = s.Objects().size();
        if (now->Loading() && objects > 1 && objects < 12) sawPartial = true;   // some of the level, plus the screen
        if (!now->Loading() && s.Find("L10")) sawLoaded = true;
        if (s.Find("Old") == nullptr) CHECK(s.Find("Loading") != nullptr);       // the old scene is gone, the screen is not
    }
    CHECK(sawPartial && sawLoaded);
    CHECK(Screen(s) == nullptr);                        // it went with its job
    std::vector<std::string> n = Names(s);
    CHECK(n.size() == 11 && s.Find("L0") && s.Find("L10") && s.Find("Old") == nullptr);
    CHECK(s.Objects().size() == 11);
}

void TestHoldsForDuration() {
    RUN_SUITE("a load faster than `duration` still shows the screen for `duration`");
    const std::string level = WriteLevel("tiny.okayscene", 1);
    const std::string trans = WriteTransition("trans_tiny.okayscene", level);
    Scene s("game");
    CHECK(SceneSerializer::LoadFromFile(s, trans));
    s.Start();
    LoadingScreen* ls = Screen(s);
    CHECK(ls != nullptr);
    if (!ls) return;
    s.Update(0.1f); s.Update(0.1f);
    CHECK(s.Find("L0") != nullptr && !ls->Loading());  // the level is in already
    CHECK(ls->Active() && ls->Progress() < 1.0f);      // but the bar follows the clock
    for (int i = 0; i < 2; ++i) s.Update(0.1f);
    CHECK(ls->Active());                                // 0.4 s: the screen is fading out
    for (int i = 0; i < 4; ++i) s.Update(0.1f);
    CHECK(Screen(s) == nullptr && s.Find("L0") != nullptr);
}

void TestFailedLoad() {
    RUN_SUITE("a scene that cannot be loaded: the scene that was showing is revealed");
    const std::string missing = Dir() + "/not_there.okayscene";
    fs::remove(missing);
    const std::string trans = WriteTransition("trans_bad.okayscene", missing);
    Scene s("game");
    CHECK(SceneSerializer::LoadFromFile(s, trans));
    s.Start();
    LoadingScreen* ls = Screen(s);
    CHECK(ls != nullptr);
    if (!ls) return;
    for (int i = 0; i < 12; ++i) s.Update(0.1f);
    CHECK(ls->LoadFailed() && !ls->Active());
    CHECK(s.Find("Old") != nullptr && s.Find("Loading") != nullptr);   // nothing was lost
    CHECK(!s.IsDontDestroyOnLoad(ls->gameObject));

    // A file that reads but does not parse fails the same way, and the old scene is untouched.
    const std::string junk = Dir() + "/junk.okayscene";
    { FILE* f = std::fopen(junk.c_str(), "wb"); std::fputs("not a scene\n", f); std::fclose(f); }
    const std::string trans2 = WriteTransition("trans_junk.okayscene", junk);
    Scene s2("game");
    CHECK(SceneSerializer::LoadFromFile(s2, trans2));
    s2.Start();
    LoadingScreen* ls2 = Screen(s2);
    CHECK(ls2 != nullptr);
    if (!ls2) return;
    for (int i = 0; i < 12; ++i) s2.Update(0.1f);
    CHECK(ls2->LoadFailed() && !ls2->Active());
    CHECK(s2.Find("Old") != nullptr && s2.Objects().size() == 2);
    CHECK(!s2.AnyLoading());
}

void TestOneGoLoading() {
    RUN_SUITE("loading in one go: asyncLoad off, or other components on the object");
    const std::string level = WriteLevel("level2.okayscene", 3);
    for (int variant = 0; variant < 2; ++variant) {
        const std::string trans = WriteTransition(variant == 0 ? "trans_sync.okayscene" : "trans_shared.okayscene", level,
                                                  /*alone=*/variant == 0, /*async=*/variant == 1);   // 0: async off; 1: async on, but not alone on its object
        Scene s("game");
        CHECK(SceneSerializer::LoadFromFile(s, trans));
        s.Start();
        LoadingScreen* ls = Screen(s);
        CHECK(ls != nullptr);
        if (!ls) continue;
        CHECK(!ls->Loading() && !s.AnyLoading());
        for (int i = 0; i < 4; ++i) s.Update(0.1f);
        CHECK(s.Find("Old") != nullptr);                  // before `duration` nothing has loaded
        for (int i = 0; i < 3; ++i) s.Update(0.1f);
        CHECK(s.Find("L0") != nullptr && s.Find("Old") == nullptr && s.Find("Loading") == nullptr);   // swapped at the end of the frame
    }
    // No such scene: the screen reveals the current scene after `duration` instead of covering it for ever.
    const std::string trans = WriteTransition("trans_nosuch.okayscene", "NoSuchScene", true, /*async=*/false);
    Scene s("game");
    SceneManager::ClearScenes();
    CHECK(SceneSerializer::LoadFromFile(s, trans));
    s.Start();
    LoadingScreen* ls = Screen(s);
    CHECK(ls != nullptr);
    if (!ls) return;
    for (int i = 0; i < 8; ++i) s.Update(0.1f);
    CHECK(!ls->Active() && s.Find("Old") != nullptr);
}

void TestSaved() {
    RUN_SUITE("the background-loading fields are saved; an older record means off");
    Scene a("a");
    GameObject* g = a.CreateGameObject("Loading");
    auto* ls = g->AddComponent<LoadingScreen>();
    ls->asyncLoad = false; ls->objectsPerFrame = 17; ls->title = "Hello";
    GameObject* h = a.CreateGameObject("Loading2");
    auto* l2 = h->AddComponent<LoadingScreen>();
    l2->asyncLoad = true; l2->objectsPerFrame = 5;
    const std::string text = SceneSerializer::Serialize(a);
    Scene b("b");
    CHECK(SceneSerializer::Deserialize(b, text));
    auto* r = b.Find("Loading") ? b.Find("Loading")->GetComponent<LoadingScreen>() : nullptr;
    auto* r2 = b.Find("Loading2") ? b.Find("Loading2")->GetComponent<LoadingScreen>() : nullptr;
    CHECK(r && !r->asyncLoad && r->objectsPerFrame == 17 && r->title == "Hello");
    CHECK(r2 && r2->asyncLoad && r2->objectsPerFrame == 5);
    // A record without the two numbers (what an older build wrote) loads with background loading off.
    std::string old = text;
    for (const char* tail : {" 0 17\n", " 1 5\n"}) {
        const std::size_t at = old.find(tail);
        CHECK(at != std::string::npos);
        if (at != std::string::npos) old.replace(at, std::string(tail).size(), "\n");
    }
    Scene c("c");
    CHECK(SceneSerializer::Deserialize(c, old));
    auto* c1 = c.Find("Loading") ? c.Find("Loading")->GetComponent<LoadingScreen>() : nullptr;
    auto* c2 = c.Find("Loading2") ? c.Find("Loading2")->GetComponent<LoadingScreen>() : nullptr;
    CHECK(c1 && !c1->asyncLoad && c1->title == "Hello");
    CHECK(c2 && !c2->asyncLoad);
    // A screen made now (not loaded) has it on.
    Scene d("d");
    CHECK(d.CreateGameObject("X")->AddComponent<LoadingScreen>()->asyncLoad);
}

} // namespace

int main() {
    TestBackgroundLoad();
    TestHoldsForDuration();
    TestFailedLoad();
    TestOneGoLoading();
    TestSaved();
    std::error_code ec;
    fs::remove_all(Dir(), ec);
    TEST_MAIN_RESULT();
}
