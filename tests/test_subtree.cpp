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

    // Viewmodel exemption: the same predicate gates the owner-camera cull, but a part
    // flagged firstPersonViewmodel (the first-person arm) is rendered even though it
    // lives inside the ignored subtree. This mirrors the renderer's skip condition.
    auto culled = [](GameObject* go, GameObject* ignore) {
        return ignore && go->IsSelfOrDescendantOf(ignore) && !go->firstPersonViewmodel;
    };
    CHECK(!grand->firstPersonViewmodel);          // default: ordinary body part
    CHECK(culled(grand, root));                   // so it IS hidden from the owner camera
    grand->firstPersonViewmodel = true;           // make it the arm/viewmodel
    CHECK(!culled(grand, root));                  // now it renders for the owner
    CHECK(!culled(grand, nullptr));               // and other cameras never cull it
    CHECK(culled(child, root));                   // a sibling body part is still hidden

    TEST_MAIN_RESULT();
}
