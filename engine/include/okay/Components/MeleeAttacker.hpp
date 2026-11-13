#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/GameInput.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Components/NPCController.hpp"
#include <string>
#include <vector>

namespace okay {

/// A melee swing for the player. With no weapon chosen it still works the old
/// way: `Swing()` hurts whoever is inside `range` and `arc` at once. A weapon
/// definition (see WeaponBook) replaces those numbers and makes the swing take
/// `swing` seconds, landing only while the hit window is open and only inside
/// the arc or the hit box. A chain of swings inside the combo window hits harder.
///
/// The hit lands on a Health component, survival health, RPG stats, or an NPC,
/// whichever the target has. A dynamic rigid body on that object is pushed by
/// the knockback. Each swing broadcasts `player_attack`.
class MeleeAttacker : public Behaviour {
public:
    float damage   = 15.0f;
    float range    = 2.5f;
    float arc      = 120.0f;     ///< attack cone, degrees (full angle)
    float cooldown = 0.5f;       ///< seconds before another swing can start
    char  attackKey = 'f';       ///< 0 = no key
    bool  useMouse = true;       ///< also swing on the left mouse button

    /// Used when no weapon definition is chosen. A swing longer than 0 with a
    /// hit window only connects while the swing's age is between the two times.
    float swingTime = 0.0f;
    float hitStart = 0.0f;
    float hitEnd = 0.0f;
    float knockback = 0.0f;
    int   comboSteps = 1;
    float comboWindow = 0.6f;
    float comboBonus = 0.25f;    ///< extra damage fraction for each hit after the first
    bool  useHitbox = false;
    float hitboxWidth = 1.2f;
    float hitboxHeight = 1.6f;
    float hitboxDepth = 1.4f;

    /// A weapon in the project book, and an optional `.okayweapon` file of its own.
    /// Empty means: use the numbers above, or the weapon tied to the held item.
    std::string weapon;
    std::string weaponFile;

    bool Ready() const { return m_timer <= 0.0f && !m_swinging; }
    bool Swinging() const { return m_swinging; }
    float SwingAge() const { return m_swing; }
    /// Which hit of the chain this swing is (0 is the first).
    int ComboStep() const { return m_comboStep; }

    void Update(float dt) override;

    /// Start a swing now (a button, a script, a test, or the attack key).
    void Swing();

private:
    float m_timer = 0.0f;
    float m_swing = 0.0f;
    float m_comboTimer = 0.0f;
    int   m_comboStep = 0;
    bool  m_swinging = false;
    std::vector<GameObject*> m_hit;
};

} // namespace okay
