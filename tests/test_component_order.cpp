// Component order (1.3, waves 4 and 5): the order of an object's components is saved in an optional `comporder`
// record and comes back after a save and load, a clone and an undo snapshot. The record is written for an object
// that was arranged on purpose (MoveComponent, ReorderComponents) and, since wave 5, for any object whose order is
// not the one the loader would produce from its records anyway (plain AddComponent calls in another order). An
// object that already loads in its own order writes no record, a file without the record loads exactly as before,
// and a record that names types the file does not have is ignored.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <string>
#include <vector>

using namespace okay;

namespace {

// The type names of an object's components after the Transform, in stored order.
std::vector<std::string> Order(const GameObject* g) {
    std::vector<std::string> out;
    for (const auto& c : g->Components()) {
        if (c.get() == static_cast<const Component*>(g->transform)) continue;
        out.push_back(ComponentSerializerRegistry::TypeKey(*c));
    }
    return out;
}

bool Contains(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }

std::string RemoveLine(const std::string& text, const std::string& startsWith) {
    std::string out;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(at, nl - at);
        std::size_t first = line.find_first_not_of(' ');
        if (first == std::string::npos || line.compare(first, startsWith.size(), startsWith) != 0) out += line + "\n";
        at = nl + 1;
    }
    return out;
}

using V = std::vector<std::string>;

} // namespace

int main() {
    RUN_SUITE("an object whose components are in the writer's order saves no record");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        g->AddComponent<Mover>();
        g->AddComponent<Spinner>();
        g->AddComponent<Lifetime>();
        CHECK(Order(g) == (V{"Mover", "Spinner", "Lifetime"}));
        CHECK(!g->HasCustomComponentOrder());
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(!Contains(text, "comporder"));                        // it already loads in its own order: nothing extra in the file
        Scene t("Loaded");
        CHECK(SceneSerializer::Deserialize(t, text));
        CHECK(Order(t.Find("G")) == (V{"Mover", "Spinner", "Lifetime"}));
        CHECK(!t.Find("G")->HasCustomComponentOrder());
        CHECK(SceneSerializer::Serialize(t) == text);               // saving it again changes nothing
        // one component, or none: never a record
        GameObject* one = s.CreateGameObject("One");
        one->AddComponent<Lifetime>();
        s.CreateGameObject("None");
        CHECK(!Contains(SceneSerializer::Serialize(s), "comporder"));
    }

    RUN_SUITE("the order of plain AddComponent calls is saved when the writer would change it");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        g->AddComponent<Lifetime>();
        g->AddComponent<Spinner>();
        g->AddComponent<Mover>();
        CHECK(Order(g) == (V{"Lifetime", "Spinner", "Mover"}));
        CHECK(!g->HasCustomComponentOrder());                       // nobody arranged it
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(Contains(text, "comporder \"Lifetime\" \"Spinner\" \"Mover\""));
        Scene t("Loaded");
        std::vector<std::string> warnings;
        CHECK(SceneSerializer::Deserialize(t, text, nullptr, &warnings));
        CHECK(warnings.empty());
        CHECK(Order(t.Find("G")) == (V{"Lifetime", "Spinner", "Mover"}));
        CHECK(SceneSerializer::Serialize(t) == text);               // and it stays that way
        // a copy made with Instantiate / Duplicate and an undo snapshot keep it too
        GameObject* copy = s.Instantiate(*g);
        CHECK(copy != nullptr && Order(copy) == (V{"Lifetime", "Spinner", "Mover"}));
        GameObject* twin = s.Duplicate(*g);
        CHECK(twin != nullptr && Order(twin) == (V{"Lifetime", "Spinner", "Mover"}));
        const std::string object = SceneSerializer::SerializeObject(*g);
        Scene u("Paste");
        GameObject* pasted = SceneSerializer::InstantiateFromText(u, object);
        CHECK(pasted != nullptr && Order(pasted) == (V{"Lifetime", "Spinner", "Mover"}));
        CHECK(t.Validate().issues.empty());
        // the record can be taken out by hand: the writer's order comes back, as for every older file
        Scene old("Old");
        CHECK(SceneSerializer::Deserialize(old, RemoveLine(text, "comporder")));
        CHECK(Order(old.Find("G")) == (V{"Mover", "Spinner", "Lifetime"}));
    }

    RUN_SUITE("every order of five components comes back, and only the odd ones write a record");
    {
        std::vector<int> perm{0, 1, 2, 3, 4};
        auto add = [](GameObject* g, int kind) {
            switch (kind) {
                case 0: g->AddComponent<Mover>(); break;
                case 1: g->AddComponent<Spinner>(); break;
                case 2: g->AddComponent<Lifetime>(); break;
                case 3: g->AddComponent<SpriteRenderer>(); break;
                default: g->AddComponent<Rigidbody2D>(); break;
            }
        };
        // the writer's order, found by loading one object with its record taken out
        V natural;
        {
            Scene s("N");
            GameObject* g = s.CreateGameObject("G");
            for (int k : perm) add(g, k);
            Scene t("N2");
            CHECK(SceneSerializer::Deserialize(t, RemoveLine(SceneSerializer::Serialize(s), "comporder")));
            natural = Order(t.Find("G"));
        }
        CHECK(natural.size() == 5);
        int perms = 0, withRecord = 0, mismatches = 0;
        do {
            Scene s("P");
            GameObject* g = s.CreateGameObject("G");
            for (int k : perm) add(g, k);
            const V live = Order(g);
            const std::string text = SceneSerializer::Serialize(s);
            const bool record = Contains(text, "comporder");
            Scene t("L");
            CHECK(SceneSerializer::Deserialize(t, text));
            if (Order(t.Find("G")) != live) ++mismatches;
            if (record != (live != natural)) ++mismatches;           // a record exactly when the order differs from the writer's
            if (record) ++withRecord;
            ++perms;
        } while (std::next_permutation(perm.begin(), perm.end()));
        CHECK(perms == 120 && mismatches == 0);
        CHECK(withRecord == 119);                                    // all but the writer's own
    }

    RUN_SUITE("objects of one shape in one scene, and scripts around another component");
    {
        Scene s("T");
        for (int i = 0; i < 20; ++i) {
            GameObject* g = s.CreateGameObject("Obj" + std::to_string(i));
            if (i % 2 == 0) { g->AddComponent<Lifetime>(); g->AddComponent<Mover>(); }   // not the writer's order
            else            { g->AddComponent<Mover>(); g->AddComponent<Lifetime>(); }   // the writer's
        }
        GameObject* sc = s.CreateGameObject("Scripted");
        auto* a = sc->AddComponent<ScriptComponent>();
        sc->AddComponent<Mover>();
        auto* b = sc->AddComponent<ScriptComponent>();
        a->SetSource("// a");
        b->SetSource("// b");
        const std::string text = SceneSerializer::Serialize(s);
        Scene t("L");
        CHECK(SceneSerializer::Deserialize(t, text));
        for (int i = 0; i < 20; ++i)
            CHECK(Order(t.Find("Obj" + std::to_string(i))) == (i % 2 == 0 ? V{"Lifetime", "Mover"} : V{"Mover", "Lifetime"}));
        const V scripted = Order(t.Find("Scripted"));
        CHECK(scripted == (V{"ScriptComponent", "Mover", "ScriptComponent"}));
        const auto scripts = t.Find("Scripted")->GetComponents<ScriptComponent>();
        CHECK(scripts.size() == 2 && scripts[0]->Source() == "// a" && scripts[1]->Source() == "// b");
        CHECK(SceneSerializer::Serialize(t) == text);
    }

    RUN_SUITE("an arranged order survives save and load");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        auto* life = g->AddComponent<Lifetime>();
        auto* spin = g->AddComponent<Spinner>();
        g->AddComponent<Mover>();
        spin->angularVelocity = {0.0f, 45.0f, 0.0f};
        life->seconds = 9.0f;
        g->MoveComponent(life, 2);                                  // Spinner, Mover, Lifetime
        CHECK(g->HasCustomComponentOrder());
        CHECK(Order(g) == (V{"Spinner", "Mover", "Lifetime"}));
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(Contains(text, "comporder"));
        Scene t("Loaded");
        std::string err;
        std::vector<std::string> warnings;
        CHECK(SceneSerializer::Deserialize(t, text, &err, &warnings));
        CHECK(warnings.empty());
        GameObject* r = t.Find("G");
        CHECK(r != nullptr);
        if (r) {
            CHECK(Order(r) == (V{"Spinner", "Mover", "Lifetime"}));
            CHECK(r->HasCustomComponentOrder());                    // so it is written again by the next save
            CHECK_NEAR(r->GetComponent<Lifetime>()->seconds, 9.0f, 1e-5f);
            CHECK_NEAR(r->GetComponent<Spinner>()->angularVelocity.y, 45.0f, 1e-5f);
        }
        // a second round trip is stable
        const std::string again = SceneSerializer::Serialize(t);
        CHECK(again == text);
        Scene u("Again");
        CHECK(SceneSerializer::Deserialize(u, again));
        CHECK(Order(u.Find("G")) == (V{"Spinner", "Mover", "Lifetime"}));
        // the Transform stays first
        CHECK(u.Find("G")->Components()[0].get() == static_cast<Component*>(u.Find("G")->transform));
        // the validator knows the record
        CHECK(t.Validate().issues.empty());
    }

    RUN_SUITE("a file without the record loads as before");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        g->AddComponent<Lifetime>();
        g->AddComponent<Spinner>();
        g->AddComponent<Mover>();
        g->MoveComponent(g->GetComponent<Lifetime>(), 2);
        const std::string withRecord = SceneSerializer::Serialize(s);
        const std::string old = RemoveLine(withRecord, "comporder");
        CHECK(!Contains(old, "comporder"));
        Scene t("Old");
        std::string err;
        std::vector<std::string> warnings;
        CHECK(SceneSerializer::Deserialize(t, old, &err, &warnings));
        CHECK(err.empty() && warnings.empty());
        CHECK(Order(t.Find("G")) == (V{"Mover", "Spinner", "Lifetime"}));   // the writer's order, as every older file
        CHECK(!t.Find("G")->HasCustomComponentOrder());
    }

    RUN_SUITE("a record that does not match the components");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        g->AddComponent<Mover>();
        g->AddComponent<Spinner>();
        g->AddComponent<Lifetime>();
        g->MoveComponent(g->GetComponent<Mover>(), 2);
        std::string text = SceneSerializer::Serialize(s);
        // a type this build has never heard of, one the object does not carry, and a type named twice
        const std::size_t at = text.find("  comporder");
        CHECK(at != std::string::npos);
        const std::size_t end = text.find('\n', at);
        text.replace(at, end - at, "  comporder \"FutureThing\" \"Lifetime\" \"Lifetime\" \"Camera\" \"Mover\"");
        Scene t("T");
        std::string err;
        CHECK(SceneSerializer::Deserialize(t, text, &err));
        CHECK(err.empty());
        // listed ones first in the listed order, the rest after them in the order they were loaded
        CHECK(Order(t.Find("G")) == (V{"Lifetime", "Mover", "Spinner"}));
        // an empty and a garbled record are ignored without failing the load
        std::string bad = SceneSerializer::Serialize(s);
        const std::size_t at2 = bad.find("  comporder");
        bad.replace(at2, bad.find('\n', at2) - at2, "  comporder");
        Scene u("U");
        CHECK(SceneSerializer::Deserialize(u, bad));
        CHECK(Order(u.Find("G")) == (V{"Mover", "Spinner", "Lifetime"}));   // the writer's order
        std::string garbled = SceneSerializer::Serialize(s);
        const std::size_t at3 = garbled.find("  comporder");
        garbled.replace(at3, garbled.find('\n', at3) - at3, "  comporder \"Mover");
        Scene w("W");
        std::string err3;
        const bool ok = SceneSerializer::Deserialize(w, garbled, &err3);
        CHECK(ok || !err3.empty());                                  // never a crash; either loads or names the problem
    }

    RUN_SUITE("order with disabled components, clones and undo snapshots");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        g->AddComponent<Lifetime>();
        auto* spin = g->AddComponent<Spinner>();
        g->AddComponent<Mover>();
        g->MoveComponent(g->GetComponent<Lifetime>(), 2);          // Spinner, Mover, Lifetime
        spin->enabled = false;
        GameObject* clone = s.Instantiate(*g);
        CHECK(clone != nullptr);
        if (clone) {
            CHECK(Order(clone) == (V{"Spinner", "Mover", "Lifetime"}));
            CHECK(!clone->GetComponent<Spinner>()->enabled);        // the disabled flag still finds the right component
            CHECK(clone->GetComponent<Mover>()->enabled);
        }
        const std::string snap = SceneSerializer::Serialize(s);
        Scene t("Restore");
        CHECK(SceneSerializer::Deserialize(t, snap));
        CHECK(Order(t.Find("G")) == (V{"Spinner", "Mover", "Lifetime"}));
        CHECK(!t.Find("G")->GetComponent<Spinner>()->enabled);
    }

    RUN_SUITE("two scripts of the same type keep their order among themselves");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        g->AddComponent<Lifetime>();
        auto* a = g->AddComponent<ScriptComponent>();
        auto* b = g->AddComponent<ScriptComponent>();
        a->SetSource("// script a");
        b->SetSource("// script b");
        g->MoveComponent(g->GetComponent<Lifetime>(), 2);          // two scripts, then the lifetime
        const std::string text = SceneSerializer::Serialize(s);
        Scene t("T");
        CHECK(SceneSerializer::Deserialize(t, text));
        const auto scripts = t.Find("G")->GetComponents<ScriptComponent>();
        CHECK(scripts.size() == 2);
        if (scripts.size() == 2) {
            CHECK(scripts[0]->Source() == "// script a");
            CHECK(scripts[1]->Source() == "// script b");
        }
        CHECK(Order(t.Find("G")).back() == "Lifetime");
    }

    RUN_SUITE("GameObject::ReorderComponents");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        auto* a = g->AddComponent<Mover>();
        auto* b = g->AddComponent<Spinner>();
        auto* c = g->AddComponent<Lifetime>();
        CHECK(g->ReorderComponents({c, a}));                        // c, a, then what was not named (b)
        CHECK(Order(g) == (V{"Lifetime", "Mover", "Spinner"}));
        CHECK(g->HasCustomComponentOrder());
        CHECK(g->Components()[0].get() == static_cast<Component*>(g->transform));
        // the Transform in the list, a pointer of another object and a null are skipped
        GameObject* other = s.CreateGameObject("Other");
        auto* foreign = other->AddComponent<Mover>();
        CHECK(g->ReorderComponents({nullptr, foreign, g->transform, b, a}));
        CHECK(Order(g) == (V{"Spinner", "Mover", "Lifetime"}));
        // the same order again changes nothing and says so
        CHECK(!g->ReorderComponents({b, a, c}));
        // a stale list does not break anything
        g->RemoveComponent(a);
        CHECK(g->ReorderComponents({c, b}));
        CHECK(Order(g) == (V{"Lifetime", "Spinner"}));
        CHECK(!g->ReorderComponents({}));                           // nothing named: nothing moves
    }

    TEST_MAIN_RESULT();
}
