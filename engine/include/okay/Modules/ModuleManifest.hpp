#pragma once
#include <string>
#include <vector>

namespace okay {

/// What one engine module says about itself. The text form is a `module.okaymodule` file
/// (`key=value` lines, `#` comments, lists separated by commas, `sources` by spaces), see
/// docs/modular_engine.md. Written from that format description only.
struct ModuleManifest {
    std::string name;          ///< lowercase id: letters, digits, underscore ("health_damage")
    std::string title;         ///< human name shown in the editor
    std::string area;          ///< group shown in the editor ("Gameplay")
    std::string description;   ///< one line: what the module adds
    int version = 1;           ///< bumped when the module's saved data or API changes
    bool defaultOn = true;     ///< ticked in a new project of the "full" preset
    bool core = false;         ///< never switched off
    std::vector<std::string> requires_;   ///< modules that must be on too
    std::vector<std::string> provides;    ///< shared contracts this module implements ("IHealth")
    std::vector<std::string> components;  ///< component types it adds
    std::vector<std::string> serializers; ///< scene record names it reads and writes
    std::vector<std::string> vsNodes;     ///< visual-script nodes it adds (names)
    std::string vsGroup;                  ///< visual-script palette group ("" = none)
    std::vector<std::string> editor;      ///< editor panels, windows and menu items it adds
    std::vector<std::string> sources;     ///< engine/-relative glob patterns of the files it owns

    /// Parse manifest text. Returns false and fills `err` (when given) on a missing or bad name.
    /// Unknown keys are ignored so older programs read newer manifests.
    static bool Parse(const std::string& text, ModuleManifest& out, std::string* err = nullptr);
    /// Split "a, b ,c" into trimmed non-empty items.
    static std::vector<std::string> SplitList(const std::string& s, char sep = ',');
    /// True when `name` is a legal module id.
    static bool ValidName(const std::string& name);
};

} // namespace okay
