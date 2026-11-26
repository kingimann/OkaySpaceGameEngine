// Base::f() in a member function of a class in a namespace ([class.qual], [basic.lookup.qual]):
// Base is looked up from the class outward (Components/CraftingStation.hpp).
#include <cstdio>
namespace okay {
struct Component { virtual ~Component() {} virtual void Update(float) {} };
struct Behaviour : Component { bool enabled = true; };
class InventoryInteractable : public Behaviour {
public:
    bool open = false;
    void Update(float) override { open = !open; std::printf("base\n"); }
};
class CraftingStation : public InventoryInteractable {
public:
    void Update(float dt) override {
        InventoryInteractable::Update(dt);
        std::printf("derived %d\n", open);
    }
};
}
int main() { okay::CraftingStation c; okay::Component *p = &c; p->Update(1); }
