#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Crafting.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Input/Input.hpp"
#include <string>

namespace okay {

/// Auto-builds a crafting panel from a sibling Crafting's recipes: at start it
/// creates one UIButton per recipe (labelled with the output, wired to craft that
/// recipe on the owner) and toggles the whole panel with `toggleKey`. Drop it next to
/// the player's Crafting + Inventory and you get a working craft menu with no UI
/// wiring.
class CraftingMenu : public Behaviour {
public:
    char  toggleKey = 'c';          // press to show/hide the panel (0 = always shown)
    bool  open = false;             // initial / current visibility
    Vec2  position{20.0f, 20.0f};   // top-left of the panel (screen pixels)
    Vec2  buttonSize{180.0f, 30.0f};
    float spacing = 6.0f;
    UIAnchor anchor = UIAnchor::TopRight;

    void Start() override { Build(); ApplyVisible(); }

    void Update(float) override {
        if (toggleKey && Input::GetKeyDown(toggleKey)) { open = !open; ApplyVisible(); }
    }

private:
    void Build() {
        if (m_built) return;
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        auto* craft = gameObject ? gameObject->GetComponent<Crafting>() : nullptr;
        if (!s || !craft) { m_built = true; return; }
        std::string owner = gameObject->name;
        for (int i = 0; i < (int)craft->recipes.size(); ++i) {
            GameObject* b = s->CreateGameObject("CraftBtn:" + craft->recipes[i].output);
            auto* btn = b->AddComponent<UIButton>();
            btn->label = "Craft " + craft->recipes[i].output;
            btn->anchor = anchor;
            btn->size = buttonSize;
            btn->position = {position.x, position.y + (buttonSize.y + spacing) * (float)i};
            btn->clickTarget = owner;
            btn->clickFunction = "Craft";
            btn->clickArg = (float)i;
            m_buttons.push_back(b);
        }
        m_built = true;
    }
    void ApplyVisible() { for (GameObject* b : m_buttons) if (b) b->active = open; }

    bool m_built = false;
    std::vector<GameObject*> m_buttons;
};

} // namespace okay
