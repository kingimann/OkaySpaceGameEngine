#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// GameObject::IsSelfOrDescendantOf powers camera subtree-culling (hiding the local
// player's whole rig, child parts included, from the first-person view).
int main() {
    RUN_SUITE("subtree");

    Scene s("X");
    GameObject* root  = s.CreateGameObject("Root");
    GameObject* child = s.CreateGameObject("Child");
    GameObject* grand = s.CreateGameObject("Grand");
    GameObject* other = s.CreateGameObject("Other");
    child->transform->SetParent(root->transform, false);
    grand->transform->SetParent(child->transform, false);

    CHECK(root->IsSelfOrDescendantOf(root));      // self
    CHECK(child->IsSelfOrDescendantOf(root));     // direct child
    CHECK(grand->IsSelfOrDescendantOf(root));     // grandchild
    CHECK(grand->IsSelfOrDescendantOf(child));    // via the middle node
    CHECK(!root->IsSelfOrDescendantOf(child));    // a parent is NOT under its child
    CHECK(!other->IsSelfOrDescendantOf(root));    // unrelated
    CHECK(!root->IsSelfOrDescendantOf(nullptr));  // null ancestor

    // Same predicate the colour pass uses. A viewmodel inside the ignored
    // subtree is still drawn. A first-person-only copy is drawn for that one
    // camera and skipped by every other one.
    auto culled = [](GameObject* go, GameObject* ignore) {
        return go->HiddenFromCamera(ignore);
    };
    CHECK(!grand->firstPersonViewmodel);          // default: ordinary body part
    CHECK(culled(grand, root));                   // so it IS hidden from the owner camera
    grand->firstPersonViewmodel = true;           // shirt, or any part this camera should see
    CHECK(!culled(grand, root));                  // now it renders for the owner
    CHECK(!culled(grand, nullptr));               // and other cameras never cull it
    CHECK(culled(child, root));                   // a sibling body part is still hidden
    grand->firstPersonViewmodel = false;
    grand->firstPersonOnly = true;                // the raised-arm copy
    CHECK(!culled(grand, root));                  // the camera hiding this player draws it
    CHECK(culled(grand, nullptr));                // a camera hiding nobody skips it
    CHECK(culled(grand, other));                  // a camera hiding someone else skips it

    TEST_MAIN_RESULT();
}
