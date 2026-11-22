// CachedComponents<T>(scene) (okay/Scene/ComponentCache.hpp) must always equal scene.FindObjectsOfType<T>(), whatever happened to
// the scene since the list was kept: components added and removed, objects created, destroyed and cleared, components
// reordered inside an object, two scenes asked in turn. A random sequence of edits, with the two lists compared after each.
// Also: the copy it returns is not changed by a later edit (a physics step holds its list across contact callbacks).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/ComponentCache.hpp"

#include <vector>

using namespace okay;

namespace {

struct Lcg {
    std::uint64_t s;
    explicit Lcg(std::uint64_t seed) : s(seed) {}
    std::uint32_t Next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return (std::uint32_t)(s >> 33); }
    int Below(int n) { return (int)(Next() % (std::uint32_t)n); }
};

template <class T> bool Same(Scene& s) {
    const std::vector<T*> a = CachedComponents<T>(s), b = s.FindObjectsOfType<T>();
    return a == b;
}

bool AllSame(Scene& s) {
    return Same<Rigidbody3D>(s) && Same<Collider3D>(s) && Same<BoxCollider3D>(s) && Same<Rigidbody2D>(s) && Same<Collider2D>(s) &&
           Same<MeshRenderer>(s) && Same<Light>(s);
}

} // namespace

int main() {
    RUN_SUITE("component cache equals a fresh walk after every kind of edit");
    {
        Lcg r(5150);
        Scene s("a"), t("b");
        Scene* scenes[2] = {&s, &t};
        int checks = 0, bad = 0;
        for (int step = 0; step < 1500; ++step) {
            Scene& sc = *scenes[r.Below(2)];
            const auto& objs = sc.Objects();
            switch (r.Below(9)) {
                case 0: case 1: { GameObject* g = sc.CreateGameObject("o"); if (r.Below(2)) g->AddComponent<BoxCollider3D>(); if (r.Below(2)) g->AddComponent<Rigidbody3D>(); break; }
                case 2: { GameObject* g = sc.CreateGameObject("p"); g->AddComponent<MeshRenderer>(); g->AddComponent<SphereCollider3D>(); g->AddComponent<BoxCollider3D>(); break; }
                case 3: { GameObject* g = sc.CreateGameObject("q"); g->AddComponent<BoxCollider2D>(); g->AddComponent<Rigidbody2D>(); break; }
                case 4: if (!objs.empty()) { GameObject* g = objs[(std::size_t)r.Below((int)objs.size())].get(); g->AddComponent<Light>(); break; } break;
                case 5: if (!objs.empty()) {   // remove a random removable component
                    GameObject* g = objs[(std::size_t)r.Below((int)objs.size())].get();
                    if (g->Components().size() > 1) g->RemoveComponent(g->Components()[1 + (std::size_t)r.Below((int)g->Components().size() - 1)].get());
                    break; } break;
                case 6: if (!objs.empty()) { sc.Destroy(objs[(std::size_t)r.Below((int)objs.size())].get()); sc.Update(0.016f); break; } break;
                case 7: if (!objs.empty()) {   // reorder inside one object: the order of a list is part of the answer
                    GameObject* g = objs[(std::size_t)r.Below((int)objs.size())].get();
                    if (g->Components().size() > 2) {
                        if (r.Below(2)) g->MoveComponent(g->Components()[1].get(), 1);
                        else { std::vector<Component*> first{g->Components().back().get()}; g->ReorderComponents(first); }
                    }
                    break; } break;
                default: if (r.Below(40) == 0) sc.Clear(); break;
            }
            ++checks;
            if (!AllSame(sc)) { ++bad; std::printf("  MISMATCH after step %d\n", step); }
            if (!AllSame(*scenes[0]) || !AllSame(*scenes[1])) { ++bad; std::printf("  MISMATCH (other scene) after step %d\n", step); }
        }
        std::printf("  %d edits, each followed by comparisons of 7 types in 2 scenes\n", checks);
        CHECK(bad == 0);
    }

    RUN_SUITE("component cache: the copy it returns does not change under the caller");
    {
        Scene s("c");
        for (int i = 0; i < 5; ++i) s.CreateGameObject("x")->AddComponent<BoxCollider3D>();
        const std::vector<Collider3D*> held = CachedComponents<Collider3D>(s);
        CHECK(held.size() == 5);
        s.CreateGameObject("y")->AddComponent<BoxCollider3D>();
        CHECK(held.size() == 5);                                   // the list in hand is as it was
        CHECK(CachedComponents<Collider3D>(s).size() == 6);        // the next ask sees the new collider
    }

    RUN_SUITE("component cache: the switch gives the plain walk");
    {
        Scene s("d");
        s.CreateGameObject("x")->AddComponent<BoxCollider3D>();
        ComponentCacheDisabled() = true;
        CHECK(CachedComponents<Collider3D>(s) == s.FindObjectsOfType<Collider3D>());
        ComponentCacheDisabled() = false;
        CHECK(CachedComponents<Collider3D>(s) == s.FindObjectsOfType<Collider3D>());
    }

    TEST_MAIN_RESULT();
}
