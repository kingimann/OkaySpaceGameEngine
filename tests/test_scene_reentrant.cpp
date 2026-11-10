// Scene::Clear(), SceneSerializer::LoadFromFile / Deserialize called from inside the scene's own callbacks
// (Update, OnDisable, OnDestroy, Awake): they used to free the object whose method was running. Now a call
// made while the scene walks its component lists is queued and applied when the walk is over (the end of the
// frame), like RequestLoad. Written to be clean under AddressSanitizer: a component that is freed while its
// method runs would be caught there, and here by a flag set in its destructor.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;

namespace {

int g_destroyedNow = 0;     // ~ClearsInUpdate runs: how many such components have been freed
int g_onDestroyCalls = 0;
int g_onDisableCalls = 0;

struct ClearsInUpdate : public Behaviour {
    bool alive = true;
    bool freedDuringCall = false;
    int afterClear = 0;
    int updates = 0;
    bool doClear = true;
    ~ClearsInUpdate() override { ++g_destroyedNow; }
    void Update(float) override {
        ++updates;
        if (!doClear) return;
        doClear = false;
        const int before = g_destroyedNow;
        gameObject->scene()->Clear();
        freedDuringCall = g_destroyedNow != before;   // would be true if Clear freed this very component
        afterClear = 1;                               // a write to `this` after Clear returned
    }
    void OnDestroy() override { ++g_onDestroyCalls; }
};

struct ClearsInOnDisable : public Behaviour {
    bool freedDuringCall = false;
    int calls = 0;
    void OnDisable() override {
        ++g_onDisableCalls;
        if (++calls > 1) return;
        const int before = g_destroyedNow;
        gameObject->scene()->Clear();
        freedDuringCall = g_destroyedNow != before;
        enabledAfter = enabled;                       // reads `this` after Clear returned
    }
    bool enabledAfter = false;
    ~ClearsInOnDisable() override { ++g_destroyedNow; }
};

struct ClearsInOnDestroy : public Behaviour {
    int destroys = 0;
    void OnDestroy() override {
        ++destroys;
        ++g_onDestroyCalls;
        if (gameObject && gameObject->scene()) gameObject->scene()->Clear();   // from inside Clear / a flush
    }
};

struct ClearsInAwake : public Behaviour {
    void Awake() override { gameObject->scene()->Clear(); }
};

struct LoadsInUpdate : public Behaviour {
    std::string path;
    bool result = false, ran = false;
    void Update(float) override {
        if (ran) return;
        ran = true;
        result = SceneSerializer::LoadFromFile(*gameObject->scene(), path);
    }
    ~LoadsInUpdate() override { ++g_destroyedNow; }
};

struct DeserializesInUpdate : public Behaviour {
    std::string text;
    bool result = false, ran = false;
    void Update(float) override {
        if (ran) return;
        ran = true;
        result = SceneSerializer::Deserialize(*gameObject->scene(), text);
    }
};

struct MakesInUpdate : public Behaviour {
    bool ran = false;
    void Update(float) override {
        if (ran) return;
        ran = true;
        gameObject->scene()->Clear();
        gameObject->scene()->CreateGameObject("Made after Clear");
    }
};

struct Survivor : public Behaviour {
    int updates = 0;
    void Update(float) override { ++updates; }
};

std::string TempPath(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

} // namespace

int main() {
    RUN_SUITE("Clear from Update: queued, the running component is not freed");
    {
        g_destroyedNow = 0; g_onDestroyCalls = 0;
        Scene s("T");
        GameObject* a = s.CreateGameObject("A");
        auto* c = a->AddComponent<ClearsInUpdate>();
        GameObject* b = s.CreateGameObject("B");
        auto* other = b->AddComponent<Survivor>();
        s.Update(0.016f);
        // After the frame ended the scene is empty and every OnDestroy ran once.
        CHECK(s.Objects().empty());
        CHECK(g_destroyedNow == 1);
        CHECK(g_onDestroyCalls == 1);
        CHECK(!s.HasPendingClear());
        (void)c; (void)other;
        // the scene is usable again
        GameObject* again = s.CreateGameObject("Again");
        again->AddComponent<Survivor>();
        s.Update(0.016f);
        CHECK(s.Objects().size() == 1);
    }
    {
        // Inspect the component while the frame is still running: Clear() returned at once, nothing was freed.
        g_destroyedNow = 0;
        Scene s("T");
        GameObject* a = s.CreateGameObject("A");
        auto* c = a->AddComponent<ClearsInUpdate>();
        struct Probe : public Behaviour {
            ClearsInUpdate* target = nullptr;
            bool sawPending = false, sawObjects = false;
            void LateUpdate(float) override {
                sawPending = gameObject->scene()->HasPendingClear();
                sawObjects = !gameObject->scene()->Objects().empty();   // the old objects are still there until the end of the frame
            }
        };
        auto* probe = a->AddComponent<Probe>();
        // read the flags before the scene is cleared and the probe freed
        bool freed = true, pending = false, objects = false, written = false;
        struct Report : public Behaviour {
            ClearsInUpdate* c = nullptr; Probe* p = nullptr;
            bool* freed = nullptr; bool* pending = nullptr; bool* objects = nullptr; bool* written = nullptr;
            void LateUpdate(float) override {
                if (c && freed) { *freed = c->freedDuringCall; *written = c->afterClear == 1; }
                if (p && pending) { *pending = p->sawPending; *objects = p->sawObjects; }
            }
        };
        auto* rep = a->AddComponent<Report>();
        rep->c = c; rep->p = probe; rep->freed = &freed; rep->pending = &pending; rep->objects = &objects; rep->written = &written;
        s.Update(0.016f);
        CHECK(!freed);
        CHECK(written);
        CHECK(pending);
        CHECK(objects);
        CHECK(s.Objects().empty());
    }

    RUN_SUITE("Clear from OnDisable and OnDestroy");
    {
        g_destroyedNow = 0; g_onDisableCalls = 0;
        Scene s("T");
        GameObject* a = s.CreateGameObject("A");
        auto* c = a->AddComponent<ClearsInOnDisable>();
        s.Update(0.016f);        // Awake, Start
        bool freed = true, enabledAfter = true;
        struct Grab : public Behaviour {
            ClearsInOnDisable* c = nullptr; bool* freed = nullptr; bool* enabledAfter = nullptr;
            void LateUpdate(float) override { if (c) { *freed = c->freedDuringCall; *enabledAfter = c->enabledAfter; } }
        };
        GameObject* holder = s.CreateGameObject("Holder");
        auto* grab = holder->AddComponent<Grab>();
        grab->c = c; grab->freed = &freed; grab->enabledAfter = &enabledAfter;
        a->active = false;       // the next frame starts by telling the component it is off
        s.Update(0.016f);
        CHECK(!freed);
        CHECK(enabledAfter);
        CHECK(g_onDisableCalls == 1);
        CHECK(s.Objects().empty());
    }
    {
        g_onDestroyCalls = 0;
        Scene s("T");
        for (int i = 0; i < 3; ++i) s.CreateGameObject("c" + std::to_string(i))->AddComponent<ClearsInOnDestroy>();
        s.Update(0.016f);
        s.Destroy(s.Objects()[0]->transform->gameObject);          // a flush runs OnDestroy, which asks for a Clear
        s.Update(0.016f);
        CHECK(s.Objects().empty());
        CHECK(g_onDestroyCalls == 3);                              // each component was told once
    }
    {   // a Clear whose own OnDestroy calls Clear again does not recurse or tell a component twice
        g_onDestroyCalls = 0;
        Scene s("T");
        for (int i = 0; i < 4; ++i) s.CreateGameObject("c" + std::to_string(i))->AddComponent<ClearsInOnDestroy>();
        s.Update(0.016f);
        s.Clear();
        CHECK(s.Objects().empty());
        CHECK(g_onDestroyCalls == 4);
        CHECK(!s.HasPendingClear());
    }
    {   // and the destructor: an OnDestroy that clears does not re-enter either
        g_onDestroyCalls = 0;
        {
            Scene s("T");
            for (int i = 0; i < 2; ++i) s.CreateGameObject("c")->AddComponent<ClearsInOnDestroy>();
            s.Update(0.016f);
        }
        CHECK(g_onDestroyCalls == 2);
    }
    {   // from Awake
        Scene s("T");
        s.CreateGameObject("A")->AddComponent<ClearsInAwake>();
        s.CreateGameObject("B")->AddComponent<Survivor>();
        s.Start();
        CHECK(s.Objects().empty());          // Start finishes the walk, then applies the queued Clear
    }

    RUN_SUITE("LoadFromFile and Deserialize from Update");
    {
        const std::string file = TempPath("okay_reentrant_load.okayscene");
        {
            Scene src("Source");
            src.CreateGameObject("Loaded1");
            GameObject* l2 = src.CreateGameObject("Loaded2");
            l2->transform->localPosition = {4.0f, 5.0f, 6.0f};
            CHECK(SceneSerializer::SaveToFile(src, file));
        }
        g_destroyedNow = 0;
        Scene s("T");
        GameObject* a = s.CreateGameObject("Old");
        auto* loader = a->AddComponent<LoadsInUpdate>();
        loader->path = file;
        GameObject* b = s.CreateGameObject("OldToo");
        b->AddComponent<Survivor>();
        bool result = false;
        struct Grab : public Behaviour {
            LoadsInUpdate* l = nullptr; bool* r = nullptr;
            void LateUpdate(float) override { if (l) *r = l->result; }
        };
        s.CreateGameObject("Holder")->AddComponent<Grab>();
        auto* grab = s.Find("Holder")->GetComponent<Grab>();
        grab->l = loader; grab->r = &result;
        s.Update(0.016f);
        CHECK(result);                                           // accepted
        CHECK(s.Find("Old") == nullptr && s.Find("OldToo") == nullptr);
        CHECK(s.Find("Loaded1") != nullptr);
        GameObject* l2 = s.Find("Loaded2");
        CHECK(l2 != nullptr);
        if (l2) CHECK_NEAR(l2->transform->localPosition.z, 6.0f, 1e-5f);
        CHECK(s.Objects().size() == 2);
        CHECK(s.LastLoadError().empty());
        CHECK(g_destroyedNow == 1);
        // a file that is not there is still reported at once
        bool missing = true;
        struct Missing : public Behaviour {
            bool* r = nullptr;
            void Update(float) override { if (r) { *r = SceneSerializer::LoadFromFile(*gameObject->scene(), "no_such_file.okayscene"); r = nullptr; } }
        };
        auto* miss = s.CreateGameObject("Miss")->AddComponent<Missing>();
        miss->r = &missing;
        s.Update(0.016f);
        CHECK(!missing);
        CHECK(s.Find("Loaded1") != nullptr);                     // nothing was queued
        std::remove(file.c_str());
    }
    {
        std::string text;
        {
            Scene src("Source");
            src.CreateGameObject("FromText");
            text = SceneSerializer::Serialize(src);
        }
        Scene s("T");
        GameObject* a = s.CreateGameObject("Old");
        auto* d = a->AddComponent<DeserializesInUpdate>();
        d->text = text;
        bool result = false;
        struct Grab : public Behaviour {
            DeserializesInUpdate* d = nullptr; bool* r = nullptr;
            void LateUpdate(float) override { if (d) *r = d->result; }
        };
        auto* grab = s.CreateGameObject("Holder")->AddComponent<Grab>();
        grab->d = d; grab->r = &result;
        s.Update(0.016f);
        CHECK(result);
        CHECK(s.Find("Old") == nullptr);
        CHECK(s.Find("FromText") != nullptr);
        // outside a frame both calls still act at once
        CHECK(SceneSerializer::Deserialize(s, text));
        CHECK(s.Objects().size() == 1);
    }

    RUN_SUITE("rules of a queued Clear");
    {
        // what is made after the call goes with the old scene (all or nothing)
        Scene s("T");
        s.CreateGameObject("Old")->AddComponent<MakesInUpdate>();
        s.Update(0.016f);
        CHECK(s.Objects().empty());
        // RequestClear is the same thing, from anywhere
        Scene t("T");
        t.CreateGameObject("X");
        t.RequestClear();
        CHECK(t.HasPendingClear());
        CHECK(t.Objects().size() == 1);
        t.Update(0.016f);
        CHECK(t.Objects().empty());
        CHECK(!t.HasPendingClear());
        // a queued Clear and a queued load: the clear first, then the load
        Scene u("T");
        u.CreateGameObject("X");
        u.RequestClear();
        std::string text;
        { Scene src("S"); src.CreateGameObject("Y"); text = SceneSerializer::Serialize(src); }
        u.RequestLoadText(text);
        u.Update(0.016f);
        CHECK(u.Find("X") == nullptr);
        CHECK(u.Find("Y") != nullptr);
        // outside a frame Clear() is immediate, as it always was
        Scene v("T");
        v.CreateGameObject("X");
        CHECK(!v.IsUpdating());
        v.Clear();
        CHECK(v.Objects().empty());
        CHECK(!v.HasPendingClear());
    }

    TEST_MAIN_RESULT();
}
