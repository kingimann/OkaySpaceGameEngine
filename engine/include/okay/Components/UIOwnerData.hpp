#pragma once
// The character a piece of UI belongs to, and its data, for UI bindings: `{owner.name}` in a UITextBind format and
// `owner.healthFrac` as a UIBarBind var. A world nameplate made a child of a character shows that character's own
// name, level and health, so ten goblins each show their own.
//
// The owner is the nearest object above the UI object (its parent first, then upward; never the UI object itself) that
// has a HealthStat, Stats, SurvivalStats, NPCController or Character. Keys (after "owner."):
//   name       the owner object's name
//   level      Stats::level (no Stats: not found)
//   health     current health, maxHealth its maximum, healthFrac health / maxHealth (0..1), healthPct 0..100
//              from the first of HealthStat, Stats, SurvivalStats, NPCController the owner has
// With no owner at all, the binding falls back to the global variable of the same name without "owner." (so
// `owner.healthFrac` reads `healthFrac`, which the player's Health Stat publishes). An owner that lacks the data gives
// nothing (a text shows its `|default`, a bar keeps its fill), never the player's value.
#include <string>

namespace okay {

class GameObject;

namespace uiowner {
/// The owner of `ui` (see above), or null.
GameObject* Find(const GameObject* ui);
/// True when `key` starts with "owner.".
inline bool IsOwnerKey(const std::string& key) { return key.compare(0, 6, "owner.") == 0; }
/// The value of an owner key ("owner.level"). `text` is set for every found key; `number` too for a numeric one
/// (`isNumber` tells which). False when there is no owner or the owner has no such data.
bool Lookup(const GameObject* ui, const std::string& key, std::string& text, float& number, bool& isNumber);
} // namespace uiowner

} // namespace okay
