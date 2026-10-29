#pragma once
// A small on-screen line for the stock player templates: "E to pick up" when a
// hold-in-hands item is in range, and "E or G to drop" while one is carried.
// Drop lives here, not in the holding solver. E is also lean-right while held;
// the drop is the press, the lean is the hold.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Input/Input.hpp"
#include <string>

namespace okay {

class HoldPrompt : public Component {
public:
    char dropKey = 'e';                 ///< press again to let go (0 = off)
    char altDropKey = 'g';              ///< the same let-go on G, the build-tool convention
    std::string promptObject = "Hold Prompt";

    /// Another prompt for the same line, shown when there is nothing to pick up or
    /// drop (a Dialogue Runner's "E to talk" while its NPC is in range).
    static std::string& ExtraPrompt() { static std::string s; return s; }

    void Update(float) override {
        ProceduralObjectHolding* hold = gameObject ? gameObject->GetComponent<ProceduralObjectHolding>() : nullptr;
        const bool holding = hold && hold->IsHolding();
        if (m_wasHolding && holding) {
            const bool main = dropKey && Input::GetKeyDown(dropKey);
            // The holder's throw key already lets go (a tap throws softly), and
            // dropping on it too would let the same press pick the item back up.
            const bool alt = altDropKey && altDropKey != dropKey && altDropKey != hold->throwKey &&
                             Input::GetKeyDown(altDropKey);
            if (main || alt) {
                hold->Drop();
                if (main) ItemPickup::BlockInteract();
            }
        }
    }

    void LateUpdate(float) override {
        ProceduralObjectHolding* hold = gameObject ? gameObject->GetComponent<ProceduralObjectHolding>() : nullptr;
        m_wasHolding = hold && hold->IsHolding();
        ItemPickup::ClearInteractBlock();
        Refresh(hold);   // after this frame's pick up, drop or throw
    }

private:
    bool m_wasHolding = false;

    static ItemPickup* Nearest(GameObject* player) {
        Scene* sc = player ? player->scene() : nullptr;
        if (!sc || !player->transform) return nullptr;
        const Vec3 at = player->transform->Position();
        ItemPickup* best = nullptr;
        float bestS = 0.0f;
        for (auto* pk : sc->FindObjectsOfType<ItemPickup>()) {
            if (!pk || !pk->enabled || !pk->holdInHands || !pk->interactKey || pk->Held()) continue;
            GameObject* go = pk->gameObject;
            if (!go || !go->active || !go->transform) continue;
            const float d = (go->transform->Position() - at).Magnitude();
            if (d > pk->interactRange) continue;
            const float s = ItemPickup::AimScore(go->transform->Position(), player);
            if (!best || s < bestS) { best = pk; bestS = s; }
        }
        return best;
    }

    void Refresh(ProceduralObjectHolding* hold) {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc) return;
        GameObject* label = sc->Find(promptObject);
        auto* tr = label ? label->GetComponent<TextRenderer>() : nullptr;
        if (!tr) return;
        std::string msg;
        if (hold && hold->IsHolding() && (dropKey || altDropKey)) {
            if (dropKey) msg += Input::KeyName(dropKey);
            if (altDropKey && altDropKey != dropKey) {
                if (!msg.empty()) msg += " or ";
                msg += Input::KeyName(altDropKey);
            }
            msg += " to drop";
        } else if (ItemPickup* pk = Nearest(gameObject)) {
            msg = Input::KeyName(pk->interactKey) + " to pick up";
        } else msg = ExtraPrompt();
        tr->text = msg;
        tr->background = !msg.empty();
    }
};

} // namespace okay
