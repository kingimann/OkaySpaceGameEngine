#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Crafting.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Input/InputArbiter.hpp"
#include <string>
#include <unordered_set>
#include <vector>

namespace okay {

/// Auto-builds a crafting panel from a sibling Crafting's recipes: it creates one
/// UIButton per recipe (labelled with the output, crafting that recipe on this
/// object's Crafting when clicked) and toggles the whole panel with `toggleKey`. Drop it
/// next to the player's Crafting + Inventory and you get a working craft menu with no
/// UI wiring. The buttons are runtime-only (never saved, so a save/load can't double
/// them), rebuilt when the recipes change, and removed with the component.
class CraftingMenu : public Behaviour {
public:
    char  toggleKey = 'k';          // press to show/hide the panel (0 = always shown); not C, the CraftingStation key
    bool  open = false;             // initial / current visibility
    Vec2  position{20.0f, 20.0f};   // top-left of the panel (screen pixels)
    Vec2  buttonSize{180.0f, 30.0f};
    float spacing = 6.0f;
    UIAnchor anchor = UIAnchor::TopRight;

    void Start() override { Rebuild(); }

    void Update(float) override {
        auto* craft = gameObject ? gameObject->GetComponent<Crafting>() : nullptr;
        if (Signature(craft) != m_signature) Rebuild();
        if (toggleKey && InputActions::Allows(GameAction::CraftToggle) && Input::GetKeyDown(toggleKey)
            && !(gameObject && gameObject->scene() && toggleKey == InputArbiter::SharedInteractKey(gameObject->scene())
                 && InputArbiter::BestInteract(gameObject->scene()).kind != InteractChoice::Kind::None)) {
            open = !open; ApplyVisible();
        }
        if (craft)
            for (int i = 0; i < (int)m_buttons.size(); ++i)
                if (m_buttons[i])
                    if (auto* btn = m_buttons[i]->GetComponent<UIButton>())
                        if (btn->WasClicked()) craft->CraftIndex(i);
    }

    void OnDestroy() override { Clear(); }
    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override {
        for (GameObject*& b : m_buttons) if (dying.count(b)) b = nullptr;
    }

    /// The generated buttons, one per recipe (runtime only).
    const std::vector<GameObject*>& Buttons() const { return m_buttons; }

private:
    static std::string Signature(const Crafting* craft) {
        std::string sig;
        if (craft) for (const auto& r : craft->AllRecipes()) { sig += r.output; sig += '\n'; }
        return sig;
    }
    void Clear() {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        for (GameObject* b : m_buttons) if (b && s) s->Destroy(b);
        m_buttons.clear();
    }
    void Rebuild() {
        Clear();
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        auto* craft = gameObject ? gameObject->GetComponent<Crafting>() : nullptr;
        m_signature = Signature(craft);
        if (!s || !craft) return;
        const auto all = craft->AllRecipes();
        for (int i = 0; i < (int)all.size(); ++i) {
            GameObject* b = s->CreateGameObject("CraftBtn:" + all[i].output);
            b->transient = true;
            auto* btn = b->AddComponent<UIButton>();
            const ItemDefinition* d = ItemDatabase::Get().Find(all[i].output);
            btn->label = "Craft " + (d ? d->DisplayName() : all[i].output);
            btn->anchor = anchor;
            btn->size = buttonSize;
            btn->position = {position.x, position.y + (buttonSize.y + spacing) * (float)i};
            m_buttons.push_back(b);
        }
        ApplyVisible();
    }
    void ApplyVisible() { for (GameObject* b : m_buttons) if (b) b->active = open; }

    std::string m_signature;
    std::vector<GameObject*> m_buttons;
};

} // namespace okay
