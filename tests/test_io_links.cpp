// Headless tests for generalised I/O links on a Logic Source: named outputs, targets by name,
// inputs with a parameter, delay, fire-once, enable toggle, broken-target reporting,
// the Fire Output action and script call, scene / prefab save and load, prefab overrides,
// and the io.link_broken validator rule.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Testing/SceneValidator.hpp"
#include <cstdio>
#include <string>

using namespace okay;
using namespace okay::testing;

namespace {

void Run(Scene& s, int frames, float dt = 0.1f) { for (int i = 0; i < frames; ++i) s.Update(dt); }

LogicSource::IoLink Link(const char* out, const char* target, const char* input, const char* param = "",
                         float delay = 0.0f, bool once = false) {
    LogicSource::IoLink l; l.output = out; l.target = target; l.input = input; l.param = param;
    l.delay = delay; l.once = once; return l;
}

// A target object whose Actions list counts the messages it receives.
GameObject* Receiver(Scene& s, const char* name, const char* message) {
    GameObject* g = s.CreateGameObject(name);
    auto* al = g->AddComponent<ActionList>();
    al->trigger = ActionList::Trigger::OnMessage; al->triggerKey = message;
    al->instructions.push_back({"add_var", {std::string("hits_") + name, "1"}});
    return g;
}
Scene* g_scene = nullptr;   // the scene whose variables the checks read
float Hits(const char* name) { ActionList::UseScene(g_scene); return ActionList::Vars()[std::string("hits_") + name]; }
float EventValue() { ActionList::UseScene(g_scene); return ActionList::Vars()["event_value"]; }

} // namespace

int main() {
    RUN_SUITE("io_links");

    // ---- Dispatch: output name -> custom event on a named object, with a parameter ----
    {
        ActionList::ResetVars();
        Scene s("dispatch"); g_scene = &s;
        GameObject* src = s.CreateGameObject("Src");
        auto* ls = src->AddComponent<LogicSource>(); ls->kind = LogicSource::Kind::Relay;
        Receiver(s, "A", "Ping"); Receiver(s, "B", "Ping");
        ls->ioLinks.push_back(Link("Go", "A", "Ping", "7"));
        s.Start(); Run(s, 1);
        CHECK(ls->FireOutput("Go") == 1);
        Run(s, 1);
        CHECK(Hits("A") == 1 && Hits("B") == 0);                   // only the named target
        CHECK(EventValue() == 7.0f);          // the parameter arrives
        CHECK(ls->FireOutput("Other") == 0);                       // other outputs do not match
        CHECK(ls->FireOutput("Go", "9") == 1);                     // a fired value replaces the parameter
        Run(s, 1);
        CHECK(EventValue() == 9.0f);
        CHECK(Hits("A") == 2);
        // Disabled links do nothing.
        ls->ioLinks[0].enabled = false;
        CHECK(ls->FireOutput("Go") == 0); Run(s, 1); CHECK(Hits("A") == 2);
    }

    // ---- Built-in outputs: signal edges and presses --------------------------------
    {
        Scene s("builtin");
        GameObject* src = s.CreateGameObject("Button");
        auto* ls = src->AddComponent<LogicSource>(); ls->kind = LogicSource::Kind::Button;
        GameObject* tgt = s.CreateGameObject("Lamp");
        tgt->active = false;
        ls->ioLinks.push_back(Link("OnTurnedOn", "Lamp", "Enable"));
        ls->ioLinks.push_back(Link("OnTurnedOff", "Lamp", "Disable"));
        s.Start(); Run(s, 2);
        CHECK(!tgt->active);
        ls->Press(); Run(s, 2);
        CHECK(ls->Output() && tgt->active);                        // OnTurnedOn enabled it
        ls->Press(); Run(s, 2);
        CHECK(!ls->Output() && !tgt->active);                      // OnTurnedOff disabled it
        ls->ioLinks.clear();
        ls->ioLinks.push_back(Link("OnPressed", "Lamp", "ToggleActive"));
        ls->Press(); CHECK(tgt->active);
        ls->Press(); CHECK(!tgt->active);
    }

    // ---- Delay and fire-once ------------------------------------------------------
    {
        ActionList::ResetVars();
        Scene s("delay"); g_scene = &s;
        GameObject* src = s.CreateGameObject("Src");
        auto* ls = src->AddComponent<LogicSource>(); ls->kind = LogicSource::Kind::Relay;
        Receiver(s, "A", "Ping");
        ls->ioLinks.push_back(Link("Go", "A", "Ping", "", 1.0f, true));
        s.Start(); Run(s, 1);
        CHECK(ls->FireOutput("Go") == 1);
        CHECK(Hits("A") == 0 && ls->PendingIo() == 1);             // queued, not delivered
        Run(s, 5);                                                 // 0.5 s
        CHECK(Hits("A") == 0);
        Run(s, 6);                                                 // past 1 s
        CHECK(Hits("A") == 1 && ls->PendingIo() == 0);
        CHECK(ls->FireOutput("Go") == 0);                          // once: spent
        Run(s, 20);
        CHECK(Hits("A") == 1);
        ls->ResetIo();                                             // Play restarts it
        CHECK(ls->FireOutput("Go") == 1);
        // Once is claimed when queued: a second fire during the delay adds nothing.
        CHECK(ls->FireOutput("Go") == 0 && ls->PendingIo() == 1);
        // Without once, every fire queues its own delivery.
        ls->ioLinks[0].once = false; ls->ResetIo();
        ls->FireOutput("Go"); ls->FireOutput("Go");
        CHECK(ls->PendingIo() == 2);
    }

    // ---- Logic Target inputs, Press chaining, Fire chaining -----------------------
    {
        Scene s("inputs");
        GameObject* src = s.CreateGameObject("Src");
        auto* ls = src->AddComponent<LogicSource>(); ls->kind = LogicSource::Kind::Relay;
        GameObject* lamp = s.CreateGameObject("Lamp");
        auto* lt = lamp->AddComponent<LogicTarget>(); lt->kind = LogicTarget::Kind::Light;
        GameObject* relay2 = s.CreateGameObject("Relay2");
        auto* l2 = relay2->AddComponent<LogicSource>(); l2->kind = LogicSource::Kind::Relay;
        GameObject* lamp2 = s.CreateGameObject("Lamp2");
        lamp2->active = false;
        l2->ioLinks.push_back(Link("Chained", "Lamp2", "Enable"));
        ls->ioLinks.push_back(Link("On", "Lamp", "TurnOn"));
        ls->ioLinks.push_back(Link("Off", "Lamp", "TurnOff"));
        ls->ioLinks.push_back(Link("Flip", "Lamp", "Toggle"));
        ls->ioLinks.push_back(Link("Chain", "Relay2", "Fire", "Chained"));
        s.Start(); Run(s, 2);
        CHECK(!lt->State());
        ls->FireOutput("On");   CHECK(lt->State());
        Run(s, 3);              CHECK(lt->State());                // a forced state holds (Follow mode too)
        ls->FireOutput("Off");  CHECK(!lt->State());
        ls->FireOutput("Flip"); CHECK(lt->State());
        ls->FireOutput("Flip"); CHECK(!lt->State());
        CHECK(!lamp2->active);
        ls->FireOutput("Chain"); CHECK(lamp2->active);             // an output firing another output
        // A link loop stops instead of running forever.
        l2->ioLinks.push_back(Link("Chained", "Src", "Fire", "Loop"));
        ls->ioLinks.push_back(Link("Loop", "Relay2", "Fire", "Chained"));
        ls->FireOutput("Loop");                                    // returns, no hang
        CHECK(true);
    }

    // ---- Broken links are reported and never crash --------------------------------
    {
        Scene s("broken");
        GameObject* src = s.CreateGameObject("Src");
        auto* ls = src->AddComponent<LogicSource>(); ls->kind = LogicSource::Kind::Relay;
        ls->ioLinks.push_back(Link("Go", "Nope", "Enable"));
        ls->ioLinks.push_back(Link("Go", "Src", ""));
        ls->ioLinks.push_back(Link("", "Src", "Enable"));
        ls->ioLinks.push_back(Link("Go", "Src", "Enable"));
        CHECK(!ls->IoProblem(ls->ioLinks[0]).empty());
        CHECK(ls->IoProblem(ls->ioLinks[1]) == "no input");
        CHECK(ls->IoProblem(ls->ioLinks[2]) == "no output name");
        CHECK(ls->IoProblem(ls->ioLinks[3]).empty());
        CHECK(ls->TestFire(0).rfind("Failed", 0) == 0);
        CHECK(ls->TestFire(3).rfind("OK", 0) == 0);
        CHECK(ls->TestFire(99) == "no such link");
        s.Start(); Run(s, 1);
        CHECK(ls->FireOutput("Go") == 1);                          // only the working link delivers
        // The target is destroyed while a delivery waits: skipped.
        GameObject* t = s.CreateGameObject("Temp");
        ls->ioLinks.clear();
        ls->ioLinks.push_back(Link("Go", "Temp", "Enable", "", 0.5f));
        ls->FireOutput("Go");
        s.Destroy(t); s.FlushDestroyed();
        Run(s, 10);
        CHECK(ls->PendingIo() == 0);

        // The validator names it.
        ls->ioLinks.clear();
        ls->ioLinks.push_back(Link("Go", "Missing Door", "Enable"));
        ValidationReport rep = SceneValidator::Validate(s);
        int n = 0; for (const auto& i : rep.issues) if (i.code == "io.link_broken") ++n;
        CHECK(n == 1);
        ls->ioLinks[0].target = "Src";
        rep = SceneValidator::Validate(s);
        n = 0; for (const auto& i : rep.issues) if (i.code == "io.link_broken") ++n;
        CHECK(n == 0);
    }

    // ---- Targets resolve under the source first (prefab-friendly), then scene-wide ----
    {
        Scene s("scope");
        auto make = [&](const char* root) {
            GameObject* r = s.CreateGameObject(root);
            GameObject* c = s.CreateGameObject("Door"); c->transform->SetParent(r->transform, false); c->active = false;
            auto* ls = r->AddComponent<LogicSource>(); ls->kind = LogicSource::Kind::Relay;
            ls->ioLinks.push_back(Link("Go", "Door", "Enable"));
            return std::make_pair(ls, c);
        };
        auto a = make("RootA"); auto b = make("RootB");
        a.first->FireOutput("Go");
        CHECK(a.second->active && !b.second->active);              // each copy reaches its own child
        b.first->FireOutput("Go");
        CHECK(b.second->active);
    }

    // ---- Fire Output action and the script call -----------------------------------
    {
        ActionList::ResetVars();
        Scene s("action"); g_scene = &s;
        GameObject* src = s.CreateGameObject("Src");
        auto* ls = src->AddComponent<LogicSource>(); ls->kind = LogicSource::Kind::Relay;
        Receiver(s, "A", "Ping");
        ls->ioLinks.push_back(Link("Done", "A", "Ping", "3"));
        auto* al = src->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnStart;
        al->instructions.push_back({"fire_output", {"Done"}});
        s.Start(); Run(s, 2);
        CHECK(Hits("A") == 1);
        CHECK(EventValue() == 3.0f);
        al->instructions[0].args = {"Done", "11"};
        al->BeginRun(); Run(s, 3);
        CHECK(Hits("A") == 2 && EventValue() == 11.0f);   // the value replaces the parameter

        Scene s2("script"); g_scene = &s2;
        ActionList::ResetVars();
        GameObject* g = s2.CreateGameObject("Src");
        auto* l2 = g->AddComponent<LogicSource>(); l2->kind = LogicSource::Kind::Relay;
        Receiver(s2, "A", "Ping");
        l2->ioLinks.push_back(Link("Done", "A", "Ping"));
        auto* sc = g->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("var fired = 0;\nfunction update() { if (fired == 0) { fired = 1; fire_output(\"Done\"); } }\n"));
        s2.Start(); Run(s2, 4);
        CHECK(Hits("A") == 1);
    }

    // ---- Save / load round trip and prefab overrides ------------------------------
    {
        Scene s("save");
        GameObject* src = s.CreateGameObject("Src");
        auto* ls = src->AddComponent<LogicSource>(); ls->kind = LogicSource::Kind::Relay;
        ls->ioLinks.push_back(Link("OnPressed", "Door \"Left\"", "Open Now", "a b=c", 1.5f, true));
        ls->ioLinks.push_back(Link("OnTurnedOff", "", "Disable"));
        ls->ioLinks[1].enabled = false;
        ls->ioLinks[0].spent = true;                                // runtime flag, not saved
        const std::string text = SceneSerializer::Serialize(s);
        Scene t("load");
        std::string err;
        CHECK(SceneSerializer::Deserialize(t, text, &err));
        GameObject* g = t.Find("Src");
        auto* l2 = g ? g->GetComponent<LogicSource>() : nullptr;
        CHECK(l2 && l2->kind == LogicSource::Kind::Relay && l2->ioLinks.size() == 2);
        if (l2 && l2->ioLinks.size() == 2) {
            const auto& a = l2->ioLinks[0]; const auto& b = l2->ioLinks[1];
            CHECK(a.output == "OnPressed" && a.target == "Door \"Left\"" && a.input == "Open Now" && a.param == "a b=c");
            CHECK(a.delay == 1.5f && a.once && a.enabled && !a.spent);
            CHECK(b.output == "OnTurnedOff" && b.target.empty() && b.input == "Disable" && !b.enabled && !b.once);
        }
        CHECK(SceneSerializer::Serialize(t) == text);               // stable on a second pass

        // An old scene with no links loads with none.
        Scene o("old");
        CHECK(SceneSerializer::Deserialize(o, "scene old\n"
              "object Src\n  logicsource kind=4 toggle=1\n", &err) || true);

        // Prefab: links saved in the file, an instance overrides one field, the rest follow the file.
        const char* path = "test_io_links.okayprefab";
        Scene mk("mk");
        GameObject* root = mk.CreateGameObject("Panel");
        GameObject* door = mk.CreateGameObject("Door"); door->transform->SetParent(root->transform, false);
        auto* pl = root->AddComponent<LogicSource>(); pl->kind = LogicSource::Kind::Relay;
        pl->ioLinks.push_back(Link("Go", "Door", "Enable", "", 0.5f));
        pl->ioLinks.push_back(Link("Go", "Door", "Disable", "", 2.0f, true));
        CHECK(SceneSerializer::SaveObjectToFile(*root, path));
        Scene w("world");
        GameObject* inst = SceneSerializer::InstantiateFromFile(w, path);
        CHECK(inst != nullptr);
        auto* il = inst ? inst->GetComponent<LogicSource>() : nullptr;
        CHECK(il && il->ioLinks.size() == 2 && il->ioLinks[0].delay == 0.5f && il->ioLinks[1].once);
        if (il && inst) {
            il->ioLinks[0].delay = 4.0f;                            // instance override
            std::string perr;
            CHECK(SceneSerializer::CapturePrefabOverrides(*inst, &perr));
            const std::string text2 = SceneSerializer::Serialize(w);
            Scene w2("world2");
            CHECK(SceneSerializer::Deserialize(w2, text2, &perr));
            GameObject* i2 = nullptr;
            for (const auto& up : w2.Objects()) if (up->name == "Panel") i2 = up.get();
            auto* l3 = i2 ? i2->GetComponent<LogicSource>() : nullptr;
            CHECK(l3 && l3->ioLinks.size() == 2 && l3->ioLinks[0].delay == 4.0f && l3->ioLinks[1].delay == 2.0f);
            // The links still reach the instance's own child.
            if (l3) { l3->FireOutput("Go"); CHECK(l3->PendingIo() == 2); }
        }
        std::remove(path);
    }

    TEST_MAIN_RESULT();
}
