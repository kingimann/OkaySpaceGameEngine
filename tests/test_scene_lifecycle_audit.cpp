// Scene lifecycle audit (1.4): event order across a scene swap, loads requested from callbacks, two loads at once,
// failed loads, DontDestroyOnLoad objects and their order, and unloading an additive scene that holds the camera.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> g_log;

// Logs every lifecycle call as "<tag>.<Event>". Saved and loaded through the component serializer registry.
struct Probe : public Behaviour {
    std::string tag;
    int updates = 0;
    void Awake() override     { g_log.push_back(tag + ".Awake"); }
    void OnEnable() override  { g_log.push_back(tag + ".OnEnable"); }
    void Start() override     { g_log.push_back(tag + ".Start"); }
    void Update(float) override { ++updates; }
    void OnDisable() override { g_log.push_back(tag + ".OnDisable"); }
    void OnDestroy() override { g_log.push_back(tag + ".OnDestroy"); }
};

void RegisterProbe() {
    ComponentSerializerRegistry::Entry e;
    e.name = "lcprobe";
    e.write = [](const GameObject& go, std::ostream& out) {
        if (auto* p = go.GetComponent<Probe>()) out << "  lcprobe " << (p->tag.empty() ? "-" : p->tag) << "\n";
    };
    e.read = [](GameObject& go, std::istream& in) {
        std::string tag; in >> tag;
        go.AddComponent<Probe>()->tag = tag == "-" ? std::string() : tag;
    };
    ComponentSerializerRegistry::Register(e);
}

std::string Dir() {
    static const std::string d = (fs::current_path() / "lifecycle_audit_tmp").string();
    fs::create_directories(d);
    return d;
}

// A scene file of `n` objects named "<prefix>0".. each with a Probe tagged "<prefix><i>", plus a camera when asked.
std::string WriteScene(const std::string& file, const std::string& prefix, int n, bool camera = false) {
    Scene sc(prefix);
    for (int i = 0; i < n; ++i) {
        GameObject* o = sc.CreateGameObject(prefix + std::to_string(i));
        o->AddComponent<Probe>()->tag = prefix + std::to_string(i);
        o->transform->localPosition = {(float)i, 0, 0};
    }
    if (camera) sc.CreateGameObject(prefix + "Cam")->AddComponent<Camera>();
    const std::string path = Dir() + "/" + file;
    SceneSerializer::SaveToFile(sc, path);
    return path;
}

bool Has(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) if (x == s) return true;
    return false;
}
int IndexOf(const std::vector<std::string>& v, const std::string& s) {
    for (std::size_t i = 0; i < v.size(); ++i) if (v[i] == s) return (int)i;
    return -1;
}
std::vector<std::string> Names(const Scene& s) {
    std::vector<std::string> n;
    for (const auto& o : s.Objects()) n.push_back(o->name);
    return n;
}
void Pump(Scene& s, int frames) { for (int i = 0; i < frames; ++i) s.Update(0.016f); }

void TestEventOrder() {
    RUN_SUITE("a replacing load: the old scene ends before the new one begins");
    const std::string path = WriteScene("order.okayscene", "new", 2);
    Scene sc("host");
    for (int i = 0; i < 2; ++i) sc.CreateGameObject("old" + std::to_string(i))->AddComponent<Probe>()->tag = "old" + std::to_string(i);
    Pump(sc, 2);
    g_log.clear();
    sc.RequestLoad(path);
    CHECK(g_log.empty());   // nothing happens until the end of the frame
    sc.Update(0.016f);
    CHECK(Has(g_log, "old0.OnDisable") && Has(g_log, "old0.OnDestroy") && Has(g_log, "old1.OnDestroy"));
    CHECK(Has(g_log, "new0.Awake") && Has(g_log, "new0.OnEnable") && Has(g_log, "new0.Start") && Has(g_log, "new1.Start"));
    // Every old end comes before every new beginning; per object: OnDisable, then OnDestroy; Awake, OnEnable, Start.
    int lastOld = -1, firstNew = 1 << 30;
    for (std::size_t i = 0; i < g_log.size(); ++i) {
        if (g_log[i].rfind("old", 0) == 0) lastOld = (int)i;
        else firstNew = std::min(firstNew, (int)i);
    }
    CHECK(lastOld >= 0 && lastOld < firstNew);
    CHECK(IndexOf(g_log, "old0.OnDisable") < IndexOf(g_log, "old0.OnDestroy"));
    CHECK(IndexOf(g_log, "new0.Awake") < IndexOf(g_log, "new0.OnEnable") && IndexOf(g_log, "new0.OnEnable") < IndexOf(g_log, "new0.Start"));
    CHECK(Names(sc) == (std::vector<std::string>{"new0", "new1"}));
    // The new objects run from the next frame, once each.
    g_log.clear();
    Pump(sc, 3);
    CHECK(g_log.empty());
    for (const auto& o : sc.Objects()) CHECK(o->GetComponent<Probe>() && o->GetComponent<Probe>()->updates == 3);
}

void TestLoadsFromCallbacks() {
    RUN_SUITE("loads requested from callbacks and from Update");
    const std::string a = WriteScene("cbA.okayscene", "a", 1);
    const std::string b = WriteScene("cbB.okayscene", "b", 2);
    const std::string c = WriteScene("cbC.okayscene", "c", 3);
    {   // the done callback of one load asks for the next
        Scene sc("host");
        bool aOk = false, bOk = false;
        sc.RequestLoad(a, [&](bool ok) { aOk = ok; sc.RequestLoad(b, [&](bool ok2) { bOk = ok2; }); });
        sc.Update(0.016f);
        CHECK(aOk && !bOk && Names(sc) == (std::vector<std::string>{"a0"}));
        CHECK(sc.HasPendingLoad());
        sc.Update(0.016f);
        CHECK(bOk && Names(sc) == (std::vector<std::string>{"b0", "b1"}));
        CHECK(!sc.HasPendingLoad());
    }
    {   // a component asks for a load from its own Update: the swap is at the end of the frame, the component is not freed under it
        struct Loader : public Behaviour {
            std::string path; int calls = 0;
            void Update(float) override { if (calls++ == 0) GetScene()->RequestLoad(path); }
        };
        Scene sc("host");
        auto* l = sc.CreateGameObject("Loader")->AddComponent<Loader>();
        l->path = c;
        sc.Update(0.016f);
        CHECK(Names(sc) == (std::vector<std::string>{"c0", "c1", "c2"}));
    }
    {   // a second request in the same frame replaces the first, which is told it never ran
        Scene sc("host");
        int firstResult = -1, secondResult = -1;
        sc.RequestLoad(a, [&](bool ok) { firstResult = ok; });
        sc.RequestLoad(b, [&](bool ok) { secondResult = ok; });
        CHECK(firstResult == 0);
        sc.Update(0.016f);
        CHECK(secondResult == 1 && Names(sc) == (std::vector<std::string>{"b0", "b1"}));
    }
    {   // an async load whose done callback starts the next level
        Scene sc("host");
        int stage = 0;
        Scene::AsyncLoadOptions o1;
        o1.objectsPerFrame = 1;
        o1.onDone = [&](bool ok) {
            if (!ok) return;
            stage = 1;
            Scene::AsyncLoadOptions o2;
            o2.objectsPerFrame = 2;
            o2.onDone = [&](bool ok2) { if (ok2) stage = 2; };
            sc.BeginAsyncLoad(c, o2);
        };
        CHECK(sc.BeginAsyncLoad(b, o1) > 0);
        Pump(sc, 12);
        CHECK(stage == 2);
        CHECK(Names(sc) == (std::vector<std::string>{"c0", "c1", "c2"}));
        CHECK(!sc.AnyLoading());
    }
    {   // a progress callback cancels the load it reports on: no crash, the done callback hears false once
        Scene sc("host");
        int doneCalls = 0, doneOk = 0, h = 0;
        Scene::AsyncLoadOptions o;
        o.objectsPerFrame = 1;
        o.onProgress = [&](float) { if (h) sc.UnloadAdditive(h); };
        o.onDone = [&](bool ok) { ++doneCalls; doneOk += ok; };
        h = sc.BeginAsyncLoad(c, o);
        Pump(sc, 6);
        CHECK(doneCalls == 1 && doneOk == 0);
        CHECK(!sc.AnyLoading() && !sc.IsLoaded(h));
    }
}

void TestLoadWhileLoading() {
    RUN_SUITE("a load while another one is in flight");
    const std::string a = WriteScene("lwA.okayscene", "a", 6);
    const std::string b = WriteScene("lwB.okayscene", "b", 4);
    const std::string c = WriteScene("lwC.okayscene", "c", 2);
    {   // two replacing async loads begun in the same frame: the later one wins, the earlier is told false
        Scene sc("host");
        sc.CreateGameObject("old");
        int aResult = -1, bResult = -1;
        Scene::AsyncLoadOptions oa, ob;
        oa.objectsPerFrame = 2; ob.objectsPerFrame = 2;
        oa.onDone = [&](bool ok) { aResult = ok; };
        ob.onDone = [&](bool ok) { bResult = ok; };
        const int ha = sc.BeginAsyncLoad(a, oa);
        const int hb = sc.BeginAsyncLoad(b, ob);
        CHECK(ha > 0 && hb > 0 && ha != hb);
        Pump(sc, 12);
        CHECK(bResult == 1 && aResult == 0);
        CHECK(sc.IsLoaded(hb) && !sc.IsLoaded(ha));
        CHECK(Names(sc) == (std::vector<std::string>{"b0", "b1", "b2", "b3"}));
        CHECK(!sc.AnyLoading());
    }
    {   // a deferred load requested while an async one is part way through abandons it
        Scene sc("host");
        int aResult = -1;
        Scene::AsyncLoadOptions oa;
        oa.objectsPerFrame = 2;
        oa.onDone = [&](bool ok) { aResult = ok; };
        const int ha = sc.BeginAsyncLoad(a, oa);
        sc.Update(0.016f);
        CHECK(sc.IsLoading(ha));
        sc.RequestLoad(c);
        Pump(sc, 12);
        CHECK(aResult == 0 && !sc.IsLoaded(ha));
        CHECK(Names(sc) == (std::vector<std::string>{"c0", "c1"}));
        // Nothing of the abandoned load is left behind or still being created.
        Pump(sc, 4);
        CHECK(Names(sc) == (std::vector<std::string>{"c0", "c1"}));
    }
    {   // an additive load and a replacing load in the same frame
        Scene sc("host");
        Scene::AsyncLoadOptions oa, ob;
        oa.additive = true; oa.objectsPerFrame = 100; ob.objectsPerFrame = 100;
        sc.BeginAsyncLoad(a, oa);
        sc.BeginAsyncLoad(b, ob);
        Pump(sc, 6);
        CHECK(!sc.AnyLoading());
        CHECK(sc.Objects().size() == 4 || sc.Objects().size() == 10);   // either order is a clean result; never a mix
    }
}

void TestFailedLoads() {
    RUN_SUITE("a load that fails leaves the old scene exactly as it was");
    const std::string good = WriteScene("failGood.okayscene", "g", 3);
    const std::string cut = Dir() + "/failCut.okayscene";
    {   // a good file cut in half: the first objects parse, the end is missing
        std::ifstream f(good, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::ofstream o(cut, std::ios::binary);
        o << text.substr(0, text.size() * 2 / 3);
    }
    const std::string junk = Dir() + "/failJunk.okayscene";
    { std::ofstream o(junk, std::ios::binary); o << "this is not a scene\n"; }
    const std::string missing = Dir() + "/failMissing.okayscene";
    fs::remove(missing);

    for (const std::string& bad : {cut, junk, missing}) {
        Scene sc("host");
        GameObject* a = sc.CreateGameObject("keepA");
        a->AddComponent<Probe>()->tag = "keepA";
        GameObject* b = sc.CreateGameObject("keepB");
        b->AddComponent<Probe>()->tag = "keepB";
        const std::string before = SceneSerializer::Serialize(sc);
        Pump(sc, 2);
        g_log.clear();
        bool result = true;
        sc.RequestLoad(bad, [&](bool ok) { result = ok; });
        sc.Update(0.016f);
        CHECK(!result);
        CHECK(!sc.LastLoadError().empty());
        CHECK(SceneSerializer::Serialize(sc) == before);
        CHECK(Names(sc) == (std::vector<std::string>{"keepA", "keepB"}));
        CHECK(sc.Objects()[0].get() == a && sc.Objects()[1].get() == b);
        CHECK(g_log.empty());   // the old objects were never disabled or destroyed
        CHECK(a->GetComponent<Probe>()->updates >= 1);

        // The same file loaded in pieces: the call or the first frame reports false, nothing is lost.
        int doneCalls = 0, doneOk = 0;
        Scene::AsyncLoadOptions o;
        o.onDone = [&](bool ok) { ++doneCalls; doneOk += ok; };
        const int h = sc.BeginAsyncLoad(bad, o);
        Pump(sc, 3);
        CHECK(doneCalls == 1 && doneOk == 0);
        CHECK(!sc.IsLoaded(h) && !sc.AnyLoading());
        CHECK(SceneSerializer::Serialize(sc) == before);
        CHECK(g_log.empty());
    }
}

void TestFailedLoadKeepsDontDestroyOrder() {
    RUN_SUITE("a failed load puts DontDestroyOnLoad objects back where they were");
    const std::string missing = Dir() + "/ddolMissing.okayscene";
    fs::remove(missing);
    Scene sc("host");
    GameObject* first = sc.CreateGameObject("first");
    GameObject* keep = sc.CreateGameObject("keep");
    GameObject* last = sc.CreateGameObject("last");
    GameObject* holder = sc.CreateGameObject("holder");
    GameObject* kid = sc.CreateGameObject("kid");
    kid->transform->SetParent(holder->transform, false);
    kid->transform->localPosition = {1, 2, 3};
    holder->transform->localPosition = {10, 0, 0};
    sc.DontDestroyOnLoad(keep);
    sc.DontDestroyOnLoad(kid);
    sc.Update(0.016f);
    const std::vector<std::string> order = Names(sc);
    const Vec3 kidWorld = kid->transform->Position();
    sc.RequestLoad(missing);
    sc.Update(0.016f);
    CHECK(!sc.LastLoadError().empty());
    CHECK(Names(sc) == order);
    CHECK(sc.IsDontDestroyOnLoad(keep) && sc.IsDontDestroyOnLoad(kid));
    CHECK(kid->transform->Parent() == holder->transform);   // still a child of the object that was not kept
    CHECK((kid->transform->Position() - kidWorld).Magnitude() < 1e-4f);
    (void)first; (void)last;
}

void TestDontDestroyOnLoad() {
    RUN_SUITE("DontDestroyOnLoad: same objects, no new events, a stable place");
    const std::string lvl = WriteScene("ddol.okayscene", "n", 2);
    for (int async = 0; async < 2; ++async) {
        Scene sc("host");
        GameObject* keep = sc.CreateGameObject("keep");
        keep->AddComponent<Probe>()->tag = "keep";
        GameObject* kid = sc.CreateGameObject("keepKid");
        kid->transform->SetParent(keep->transform, false);
        kid->AddComponent<Probe>()->tag = "keepKid";
        sc.CreateGameObject("gone")->AddComponent<Probe>()->tag = "gone";
        sc.DontDestroyOnLoad(keep);
        Pump(sc, 2);
        const std::uint64_t keepId = keep->InstanceId(), kidId = kid->InstanceId();
        const int updatesBefore = keep->GetComponent<Probe>()->updates;
        g_log.clear();
        if (async) {
            Scene::AsyncLoadOptions o; o.objectsPerFrame = 1;
            sc.BeginAsyncLoad(lvl, o);
            Pump(sc, 8);
        } else {
            sc.RequestLoad(lvl);
            Pump(sc, 2);
        }
        CHECK(!Has(g_log, "keep.OnDestroy") && !Has(g_log, "keep.OnDisable") && !Has(g_log, "keep.Awake") && !Has(g_log, "keepKid.OnDestroy"));
        CHECK(Has(g_log, "gone.OnDestroy"));
        CHECK(GameObject::FromInstanceId(keepId) == keep && GameObject::FromInstanceId(kidId) == kid);
        CHECK(keep->GetComponent<Probe>()->updates > updatesBefore);   // it keeps running through the swap
        CHECK(kid->transform->Parent() == keep->transform);
        CHECK(sc.IsDontDestroyOnLoad(keep));
        CHECK(sc.Find("gone") == nullptr && sc.Find("n0") && sc.Find("n1"));
        // The kept objects come after the loaded ones, in their own order, however the load was done.
        std::vector<std::string> names = Names(sc);
        CHECK(names == (std::vector<std::string>{"n0", "n1", "keep", "keepKid"}));
        // A second load keeps them again.
        sc.RequestLoad(lvl);
        Pump(sc, 2);
        CHECK(GameObject::FromInstanceId(keepId) == keep && sc.Find("keep") == keep);
        CHECK(Names(sc) == (std::vector<std::string>{"n0", "n1", "keep", "keepKid"}));
    }
}

void TestUnloadAdditiveWithCamera() {
    RUN_SUITE("unloading an additive scene that holds the active camera");
    const std::string extra = WriteScene("addCam.okayscene", "x", 1, /*camera=*/true);
    Scene sc("host");
    GameObject* hostCam = sc.CreateGameObject("HostCam");
    auto* hc = hostCam->AddComponent<Camera>();
    Pump(sc, 2);
    CHECK(sc.mainCamera == hc);
    Scene::AsyncLoadOptions o;
    o.additive = true; o.objectsPerFrame = 10;
    const int h = sc.BeginAsyncLoad(extra, o);
    Pump(sc, 2);
    CHECK(sc.IsLoaded(h));
    Camera* extraCam = sc.Find("xCam") ? sc.Find("xCam")->GetComponent<Camera>() : nullptr;
    CHECK(extraCam != nullptr);
    sc.mainCamera = extraCam;   // the additive scene's camera takes over
    CHECK(sc.UnloadAdditive(h));
    Pump(sc, 2);
    CHECK(sc.Find("xCam") == nullptr && sc.Find("x0") == nullptr);
    CHECK(sc.mainCamera == hc);   // back to a camera that still exists
    // With no camera left at all the pointer is empty, never dangling.
    Scene empty("empty");
    const int h2 = empty.BeginAsyncLoad(extra, o);
    Pump(empty, 2);
    CHECK(empty.IsLoaded(h2) && empty.mainCamera != nullptr);
    empty.UnloadAdditive(h2);
    Pump(empty, 2);
    CHECK(empty.mainCamera == nullptr && empty.Objects().empty());
}

} // namespace

int main() {
    RegisterProbe();
    TestEventOrder();
    TestLoadsFromCallbacks();
    TestLoadWhileLoading();
    TestFailedLoads();
    TestFailedLoadKeepsDontDestroyOrder();
    TestDontDestroyOnLoad();
    TestUnloadAdditiveWithCamera();
    std::error_code ec;
    fs::remove_all(Dir(), ec);
    TEST_MAIN_RESULT();
}
