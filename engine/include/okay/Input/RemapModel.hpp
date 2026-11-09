#pragma once
// What a "Controls" screen needs from the input system (docs/accessibility.md): the list of
// rebindable actions with their current and default bindings, which other actions already use
// a binding, and a rebind that deals with those conflicts the way the player chooses.
//
// It sits on InputActions' public API (All, Find, Rebind, Current, Reset, Save): rebinding
// still goes through InputActions, and a player's choices are still saved in Prefs.
#include "okay/Input/InputActions.hpp"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace okay {

/// One device column of a controls row.
struct RemapSlot {
    bool bound = false;
    InputBinding binding;                  ///< what the action uses now on this device
    InputBinding defaultBinding;           ///< what it started with (None kind when it had none)
    bool isDefault = true;                 ///< the same as the default
    std::string label;                     ///< "Space", "Left Stick"; "" when unbound
    std::vector<std::string> partLabels;   ///< a four-key direction: labels of up, down, left, right
};

/// One action of the controls list.
struct RemapRow {
    std::string action;
    bool direction = false;     ///< a 2D action (Move, Look): bound to a stick, the d-pad or four keys
    bool reserved = false;      ///< the game doesn't let the player change it
    RemapSlot keyboard;         ///< the keyboard / mouse binding the player changes
    RemapSlot gamepad;          ///< the controller binding
    bool Changed() const { return !keyboard.isDefault || !gamepad.isDefault; }
};

struct RemapOptions {
    std::set<std::string> reserved;   ///< actions (any case) that can't be rebound ("Pause")
    std::set<std::string> hidden;     ///< actions left out of the list
};

/// Another action already using a binding.
struct RemapConflict {
    std::string action;
    std::string binding;    ///< the binding it uses, as InputBinding::ToString writes it
    bool gamepad = false;
    int part = -1;          ///< when the proposed key is one key of that action's four-key direction: 0..3 (up, down, left, right)
    bool reserved = false;  ///< that action can't be rebound, so the conflict can't be resolved by moving it
};

/// Two actions that use the same input: a pair a controls screen marks.
struct RemapDuplicate {
    std::string first, second;
    std::string binding;    ///< the shared input as a player reads it ("Space")
};

enum class RemapResolve {
    Cancel,   ///< change nothing when the binding is taken
    Swap,     ///< the action that had it gets this action's old binding
    Unbind,   ///< the action that had it loses that binding (only when it keeps another)
    Allow,    ///< bind it anyway: both actions use it
};

struct RemapResult {
    bool ok = false;                        ///< the binding was changed
    std::string error;                      ///< why not
    std::vector<RemapConflict> conflicts;   ///< what was in the way (also when it was resolved)
    std::vector<std::string> changed;       ///< the actions whose bindings changed
};

/// True when two inputs are pressed by the same thing: the same key, button or stick; a key that
/// is one of a four-key direction's keys; the d-pad and its buttons.
bool RemapOverlap(const InputBinding& a, const InputBinding& b);

/// Every action (except `hidden` ones), in definition order, with its two device columns.
std::vector<RemapRow> RemapRows(const RemapOptions& options = {});

/// The bindings the stick / d-pad direction actions can take, as InputBinding text:
/// "stick:left", "stick:right", "dpad".
std::vector<std::string> RemapDirectionChoices();

/// The other actions that already use `binding` (text: "key:space", "pad:a", "stick:left").
/// `part` 0..3 asks about one key of `action`'s four-key direction (then `binding` is a key:
/// the action's own other keys count as conflicts too, with `action` as the conflicting action).
std::vector<RemapConflict> RemapConflicts(const std::string& action, const std::string& binding, int part = -1,
                                          const RemapOptions& options = {});

/// Every pair of actions that share an input right now.
std::vector<RemapDuplicate> RemapAllConflicts(const RemapOptions& options = {});

/// Rebind `action` to `binding` (see InputActions::Rebind for which slot changes), dealing with
/// actions that already use it as `how` says. Nothing is changed unless `ok`.
RemapResult RemapBind(const std::string& action, const std::string& binding, RemapResolve how = RemapResolve::Cancel,
                      int part = -1, const RemapOptions& options = {});

} // namespace okay
