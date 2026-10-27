#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

class GameObject;

/// What a character is wearing and holding: named slots (Head, Chest, ..., Main Hand),
/// each accepting items whose definition has that equip slot. Dragging an item from the
/// bag onto a slot equips it (EquipFrom); dragging it back unequips it (Unequip).
///  - The item's prefab (custom field "equipPrefab", else its world prefab) is shown on
///    the character at the slot's socket (FindSocket: a named child, a Character bone,
///    or a ModelAnimator node), without physics. It is runtime-only and never saved.
///  - The definitions' custom "armor" fields add to HealthStat / SurvivalStats armor and
///    "defense" to Stats defense, and worn items' weight still counts toward the
///    Inventory's encumbrance, all as runtime values removed when the item comes off.
///  - A worn item whose prefab has a GridInventory (a backpack) is a worn container:
///    the grid UI lists it with the player's bags. Its contents travel with the item
///    (in its instance data) when it's unequipped, dropped or saved.
///  - Scripts get on_equip(slot, item) and on_unequip(slot, item).
class Equipment : public Behaviour {
public:
    struct Slot {
        std::string name;                       ///< shown in the UI and used by scripts ("Head")
        EquipSlot accepts = EquipSlot::None;    ///< None = any equippable item
        std::string socket;                     ///< where its prefab goes (empty = the default for `accepts`)
        std::string item;                       ///< equipped item id ("" = empty)
        ItemInstanceData data;
        std::uint64_t shown = 0;                ///< runtime: InstanceId of the object shown (0 = none)
        std::string shownItem;                  ///< runtime: which item `shown` was made for
    };

    std::vector<Slot> slots = DefaultSlots();
    bool showOnCharacter = true;   ///< attach the items' prefabs to the character
    /// Carry what's worn into the next scene loaded with load_scene (like the Inventory flag).
    bool keepOnSceneLoad = false;

    static std::vector<Slot> DefaultSlots();
    /// Default socket for an equip slot type: a Character bone token ("head", "r_hand", ...).
    static const char* DefaultSocket(EquipSlot s);
    /// The Equipment on `go`, else on its root.
    static Equipment* Of(GameObject* go);

    int  Find(const std::string& slotName) const;
    bool Accepts(int slot, const std::string& item) const;
    /// The slot `item` goes to: the first empty one accepting it, else the first accepting it (-1 none).
    int  SlotFor(const std::string& item) const;
    const std::string& ItemIn(int slot) const;

    /// Equip one item from `inv` slot `from` into equipment slot `to` (-1 = SlotFor). Whatever
    /// was worn there goes back into the bag (into `from` if that's now empty). Rejected,
    /// with nothing changed, when the slot doesn't accept the item or the bag has no room.
    InvResult EquipFrom(Inventory& inv, int from, int to = -1);
    /// Move what's worn in `slot` into `inv` (slot `into`, or anywhere when that's taken or -1).
    /// Rejected, with nothing changed, when the bag has no room.
    InvResult Unequip(int slot, Inventory& inv, int into = -1);
    /// Put `item` straight into `slot` / empty it, without a bag (scripts, loading).
    bool Set(int slot, const std::string& item, const ItemInstanceData& data = {});
    void Clear(int slot);
    /// Load `item` into `slot` as saved: no events, a shown bag isn't folded into it.
    void Restore(int slot, const std::string& item, const ItemInstanceData& data);

    /// The slot's instance data including a worn bag's current contents (what saving stores).
    ItemInstanceData CurrentData(int slot) const;
    /// The object shown for `slot` on the character (null if none or destroyed).
    GameObject* Shown(int slot) const;

    float ArmorBonus() const;
    int   DefenseBonus() const;
    float WeightWorn() const;

    /// Bring the shown objects and stat bonuses up to date with `slots`.
    void Refresh();

    void Start() override { Refresh(); }
    void Update(float) override { if (m_dirty) Refresh(); }
    void OnDestroy() override;

private:
    bool m_dirty = true;
    void Show(int i);
    void Hide(int i);
    void ApplyBonuses(float armor, int defense, float weight);
    void Fire(const char* ev, int slot, const std::string& item);
    bool PutBack(Inventory& inv, int pref, const std::string& item, const ItemInstanceData& data, int& placed);
};

} // namespace okay
