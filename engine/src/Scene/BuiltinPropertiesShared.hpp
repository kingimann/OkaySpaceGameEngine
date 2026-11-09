#pragma once
// What the built-in registration files share: BuiltinProperties.cpp (the core types),
// BuiltinPropertiesGameplay.cpp (controllers, vehicles, navigation, networking) and
// BuiltinPropertiesUI.cpp (the UI widgets). Private to the engine: not installed.
#include "okay/Scene/PropertyBuilder.hpp"
#include <string>
#include <vector>

namespace okay {
namespace detail {

/// A sane ceiling for sizes and distances.
constexpr double kBig = 100000.0;

/// The option names of UIAnchor and UIShape, in enum order.
const std::vector<std::string>& AnchorNames();
const std::vector<std::string>& ShapeNames();

/// Defined in BuiltinPropertiesGameplay.cpp and BuiltinPropertiesUI.cpp; called by
/// RegisterBuiltinPropertyTable().
void RegisterGameplayProperties();
void RegisterUIProperties();

} // namespace detail
} // namespace okay
