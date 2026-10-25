#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// OnMouse* dispatch must not touch objects that no longer exist. Regression for
// the 2.813.0 editor crash (Scene::DispatchPointer calling OnMouseExit on the
// freed object it hovered last, after the scene was cleared/reloaded, e.g. when
// Play stops and restarts or a game loads another level).
namespace {
struct Counter : Component {
    int* enter = nullptr; int* exit = nullptr;
    void OnMouseEnter() override { if (enter) ++*enter; }
    void OnMouseExit() override  { if (exit) ++*exit; }
};
GameObject* MakeTarget(Scene& s, int* enter, int* exit) {
    GameObject* go = s.CreateGameObject("Target");
    go->AddComponent<BoxCollider2D>()->size = Vec2{2.0f, 2.0f};
    auto* c = go->AddComponent<Counter>();
    c->enter = enter; c->exit = exit;
    return go;
}
} // namespace

int main() {
    RUN_SUITE("scene_pointer");
    UICanvas::Set(1280.0f, 720.0f);
    Input::FeedMouse(Vec2{640.0f, 360.0f}, 0);   // canvas centre = world origin (no camera: ortho 5)

    // --- Hover, then the scene is cleared and refilled --------------------------
    {
        Scene s("pointer");
        int enter1 = 0, exit1 = 0;
        MakeTarget(s, &enter1, &exit1);
        s.Start();
        s.Update(0.016f);
        CHECK(enter1 == 1);                     // cursor over the target
        CHECK(s.MouseHoverObject() != nullptr);

        s.Clear();                              // what a scene load / Play restore does
        CHECK(s.MouseHoverObject() == nullptr); // nothing left pointing at freed objects
        int enter2 = 0, exit2 = 0;
        MakeTarget(s, &enter2, &exit2);
        s.Start();
        s.Update(0.016f);                       // used to call OnMouseExit on the freed object
        CHECK(exit1 == 0);                      // the old object is gone: nothing sent to it
        CHECK(enter2 == 1);                     // the new one under the cursor gets Enter
        s.Update(0.016f);
        CHECK(enter2 == 1 && exit2 == 0);

        // Moving off it sends Exit once.
        Input::FeedMouse(Vec2{10.0f, 10.0f}, 0);
        s.Update(0.016f);
        CHECK(exit2 == 1);
        Input::FeedMouse(Vec2{640.0f, 360.0f}, 0);
    }

    // --- A press that began on a cleared scene doesn't fire Up/Click later -------
    {
        Scene s("press");
        int e = 0, x = 0;
        MakeTarget(s, &e, &x);
        s.Start();
        Input::FeedMouse(Vec2{640.0f, 360.0f}, 1);   // press over the target
        s.Update(0.016f);
        s.Clear();
        int e2 = 0, x2 = 0;
        MakeTarget(s, &e2, &x2);
        s.Start();
        Input::FeedMouse(Vec2{640.0f, 360.0f}, 0);   // release after the reload
        s.Update(0.016f);                            // used to send OnMouseUp to the freed press target
        CHECK(e2 == 1);
    }

    // --- Destroying the hovered object (already handled) still works ------------
    {
        Scene s("destroy");
        int e = 0, x = 0;
        GameObject* t = MakeTarget(s, &e, &x);
        s.Start();
        s.Update(0.016f);
        CHECK(e == 1);
        s.Destroy(t);
        s.Update(0.016f);
        CHECK(s.MouseHoverObject() == nullptr);
        s.Update(0.016f);
        CHECK(x == 0);
    }

    Input::FeedMouse(Vec2{0.0f, 0.0f}, 0);
    TEST_MAIN_RESULT();
}
