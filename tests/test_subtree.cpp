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

    TEST_MAIN_RESULT();
}
