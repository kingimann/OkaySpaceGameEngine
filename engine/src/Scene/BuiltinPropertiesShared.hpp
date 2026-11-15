#pragma once
// What the built-in registration files share: BuiltinProperties.cpp (the core types),
// BuiltinPropertiesGameplay.cpp (controllers, vehicles, navigation, networking) and
// BuiltinPropertiesUI.cpp (the UI widgets). Private to the engine: not installed.
#include "okay/Scene/PropertyBuilder.hpp"
#include <string>
#include <vector>

namespace okay {
namespace detail {

/// The ceiling for sizes, distances, speeds and amounts. It is far above any
/// value a scene uses, because every registry write (Inspector edit, undo, a
/// Saveable's property paths, tween_prop, prefab edits) clamps to the range: at
/// the old 1e5 a scene with a position 250 km out or 250000 health was cut down
/// the first time it passed through the registry (test registry_large_values).
/// A float still holds 1e9 exactly. Fields that need a real limit set their own.
constexpr double kBig = 1.0e9;

/// The option names of UIAnchor and UIShape, in enum order.
const std::vector<std::string>& AnchorNames();
const std::vector<std::string>& ShapeNames();

/// Defined in BuiltinPropertiesGameplay.cpp and BuiltinPropertiesUI.cpp; called by
/// RegisterBuiltinPropertyTable().
void RegisterGameplayProperties();
void RegisterUIProperties();

} // namespace detail
} // namespace okay
