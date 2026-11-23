#pragma once
// The .okayinput file: control schemes, action maps, actions, bindings, processors and interactions as readable text, plus a
// shipped default and a migration from the older InputActions table. Written for this engine; the format is described here.
//
//   okayinput 1
//   scheme Keyboard+Mouse keyboard mouse        (flags: keyboard mouse gamepad touch; names have no spaces)
//   map Gameplay priority=0 consume=0 enabled=1
//   action Move vector2 map=Gameplay press=0.5
//     bind vector2 key:w key:s key:a key:d group=Keyboard+Mouse proc=normalize
//     bind single stick:left group=Gamepad proc=deadzone(0.2,0.9);curve(2)
//     interaction hold(0.5)
//     processor scale(2)                         (applies to the action after its bindings)
//   Bind kinds: single (1 part), vector2 (up down left right), vector3 (up down left right forward back), axis (negative positive),
//   chord (modifiers..., then the input). Lines starting with # are comments.
#include "okay/Input/ActionSet.hpp"
#include <string>
#include <vector>

namespace okay {

/// The asset as text. Round-trips with ParseInputAsset.
std::string SerializeInputAsset(const ActionSet& set);
/// Parse text into `out` (replacing its schemes, maps and actions). On an error returns false, sets `error` ("line N: ...") and leaves `out` alone.
bool ParseInputAsset(const std::string& text, ActionSet& out, std::string* error = nullptr);
bool LoadInputAsset(const std::string& path, ActionSet& out, std::string* error = nullptr);
bool SaveInputAsset(const std::string& path, const ActionSet& set);

/// The default asset new projects start with (Gameplay: Move, Look, Jump, Sprint, Crouch, Fire, Aim, Interact, Reload, Inventory,
/// Pause; UI: Navigate, Submit, Cancel; schemes Keyboard+Mouse, Gamepad, Touch). The same text is shipped as examples/input/default.okayinput.
const std::string& DefaultInputAssetText();
ActionSet DefaultInputAsset();

/// Build an asset from the existing InputActions table (every action with its bindings, response and context), so a project that
/// already defines actions keeps working: contexts become maps, directions become vector2 actions, the response becomes processors.
ActionSet MigrateInputActions();

/// One InputActions binding as ActionSet binding parts: a four-key direction ("keys:w,s,a,d") is its four keys
/// ("key:w", "key:s", "key:a", "key:d", a vector2 composite), anything else its one path.
std::vector<std::string> BindingParts(const InputBinding& b);

} // namespace okay
