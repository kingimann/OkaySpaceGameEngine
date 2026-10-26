// Scene::StructureVersion() keys the animation caches (ModelAnimator node maps,
// binds, crossfade targets, AnimStateMachine layer masks). It used to change every
// frame, which rebuilt those caches every frame; it must change only when the
// hierarchy does (an object created, destroyed or reparented).
#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("structure_version");
    Scene s("v"); s.physicsEnabled = false;
    GameObject* a = s.CreateGameObject("A");
    GameObject* b = s.CreateGameObject("B");
    s.Start();
    s.Update(0.016f);
    const auto v0 = s.StructureVersion();
    for (int i = 0; i < 10; ++i) s.Update(0.016f);
    CHECK(s.StructureVersion() == v0);                 // idle frames: unchanged

    b->transform->SetParent(a->transform);
    const auto v1 = s.StructureVersion();
    CHECK(v1 != v0);                                    // reparent: changed
    b->transform->SetParent(a->transform);
    CHECK(s.StructureVersion() == v1);                  // same parent again: no-op

    s.CreateGameObject("C");
    const auto v2 = s.StructureVersion();
    CHECK(v2 != v1);                                    // create: changed

    s.Destroy(b);
    s.Update(0.016f);
    CHECK(s.StructureVersion() != v2);                  // destroy: changed
    const auto v3 = s.StructureVersion();
    s.Update(0.016f);
    CHECK(s.StructureVersion() == v3);
    TEST_MAIN_RESULT();
}
