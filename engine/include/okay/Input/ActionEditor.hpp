#pragma once
// The logic behind the Input Actions window and the live debugger, with no UI: editing an input asset (add, remove, rename, bind,
// validate, save) and describing what the input system is doing right now as text lines. The editor draws these with ImGui; tests
// and screenshots use them directly.
#include "okay/Input/InputAsset.hpp"
#include <string>
#include <vector>

namespace okay {

/// An input asset being edited, with its file and a dirty flag.
class ActionEditor {
public:
    ActionSet set;
    std::string path;      ///< "" = not saved yet
    bool dirty = false;

    void NewFromDefault() { set = DefaultInputAsset(); path.clear(); dirty = true; }
    void NewFromMigration() { set = MigrateInputActions(); path.clear(); dirty = true; }
    bool Load(const std::string& file, std::string* error = nullptr);
    bool Save();                                   ///< to `path`; false when there is no path or it cannot be written
    bool SaveAs(const std::string& file);

    // ---- every edit returns false (and changes nothing) when it is not allowed
    bool AddMap(const std::string& name, int priority = 0, bool consume = false);
    bool AddAction(const std::string& name, ValueType type, const std::string& map);
    bool AddBinding(const std::string& action, BindingSpec::Kind kind, const std::string& group = "");   ///< with empty inputs ready to fill in
    bool RemoveBinding(const std::string& action, int binding);
    bool SetPart(const std::string& action, int binding, int part, const std::string& path);   ///< the path must parse (key:w ...)
    bool SetGroup(const std::string& action, int binding, const std::string& group);
    bool SetProcessors(const std::string& action, int binding, const std::string& text);       ///< binding -1 = the action's own
    bool AddInteraction(const std::string& action, const std::string& text);
    bool RemoveInteraction(const std::string& action, int index);
    bool AddScheme(const std::string& name, bool keyboard, bool mouse, bool gamepad, bool touch);
    bool Remove(const std::string& action) { return Touch(set.RemoveAction(action)); }
    bool Rename(const std::string& from, const std::string& to) { return Touch(set.RenameAction(from, to)); }

    /// Problems worth fixing, one line each: names with spaces or empty, a binding group that is no scheme, an unknown map, empty or unreadable
    /// inputs, a chorded interaction naming a missing action, two actions with the same name.
    std::vector<std::string> Validate() const;
    /// The tree the window shows: maps, their actions with type and binding count.
    std::vector<std::string> Outline() const;
    /// The selected action in full: each binding with kind, inputs, group and processors, then interactions and action processors.
    std::vector<std::string> Describe(const std::string& action) const;
    /// One row of a Controls screen built from this set. A 1D axis, a 3D vector and a pad button inside a
    /// direction are shown by name. `keyboard` or `gamepad` is empty only when that device has no binding.
    struct ControlRow {
        std::string action;
        std::string type;
        std::string keyboard;
        std::string gamepad;
    };
    std::vector<ControlRow> ControlRows() const;
private:
    bool Touch(bool changed) { if (changed) dirty = true; return changed; }
};

/// What the input system sees right now (for the live debugger): each plugged-in controller with its family, ids and mapping source;
/// the keyboard and mouse; the last used device.
std::vector<std::string> ControllerDebugLines();
/// One ActionSet right now: scheme, enabled maps, and every action with its value, phase and the binding that drives it.
std::vector<std::string> ActionSetDebugLines(const ActionSet& set, int maxActions = 40);

} // namespace okay
