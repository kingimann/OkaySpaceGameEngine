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

// ---- Key bindings --------------------------------------------------------------------------------
// A key binding is stored as a `char` key code (0 = none; 1..31 are the special keys such as
// Shift, Ctrl and the arrows, see Input.hpp). It is an Int property 0..255, so an inspector shows
// a number and a script sets it with the same codes the controllers use.
//
// A few scene records (`textRecord`) write the key as one raw character instead of a number
// (`tdctrl`, `tdctrl2d`, `charctrl2d`, `ctmctrl`, `tpsctrl`). White space would split the record
// and '-' means "none" there, so those codes are refused (they could not be saved).
inline bool KeyCodeStorable(int code, bool textRecord) {
    return !(textRecord && (code == '-' || code == ' ' || (code >= 9 && code <= 13)));
}

template <class C, class Get, class Set>
PropertyBuilder<C>& AddKeyVia(PropertyBuilder<C>& b, const char* name, Get get, Set set, bool textRecord) {
    b.Custom(name, PropertyType::Int,
             [get](const C& c) { return PropertyValue::MakeInt(static_cast<int>(static_cast<unsigned char>(get(c)))); },
             [set, textRecord](C& c, const PropertyValue& v) {
                 const int code = static_cast<int>(v.AsInt());
                 if (!KeyCodeStorable(code, textRecord)) return false;
                 set(c, static_cast<char>(code));
                 return true;
             });
    return b.Range(0, 255, 1).Tooltip("Key code (0 = none)");
}

/// `AddKey(b, "sprintKey", &Controller::sprintKey)`: a `char` field holding a key code.
template <class C, class Owner>
PropertyBuilder<C>& AddKey(PropertyBuilder<C>& b, const char* name, char Owner::*m, bool textRecord = false) {
    return AddKeyVia<C>(b, name, [m](const C& c) { return c.*m; }, [m](C& c, char k) { c.*m = k; }, textRecord);
}

/// The same for a `char` inside a struct member: `AddKey(b, "dodgeKey", &C::dodge, &DodgeMove::key)`.
template <class C, class Owner, class Inner>
PropertyBuilder<C>& AddKey(PropertyBuilder<C>& b, const char* name, Inner Owner::*outer, char Inner::*inner, bool textRecord = false) {
    return AddKeyVia<C>(b, name, [outer, inner](const C& c) { return (c.*outer).*inner; },
                        [outer, inner](C& c, char k) { (c.*outer).*inner = k; }, textRecord);
}

} // namespace detail
} // namespace okay
