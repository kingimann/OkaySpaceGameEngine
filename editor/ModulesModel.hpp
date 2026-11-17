#pragma once
// What the "which modules does this project use" pages show and do, with no drawing: the New Project form's
// second step and Edit > Project Settings > Modules both use it (ModulesPanelUI.hpp draws it). Header-only,
// built on the engine's ModuleRegistry; written for this repository from docs/modular_engine.md.
//
//   Selection sel(ModuleRegistry::Get(), project.modules);   // "" = every module (an old project)
//   sel.Set("weapons", true)  -> turns on what weapons needs too and says which
//   sel.Set("health_damage", false) -> refused while an enabled module needs it, with the reason
//   project.modules = sel.ToText();                          // "a,b,c" for project.okayproj
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace okaymod {

using okay::ModuleRegistry;

/// A shortcut that pre-ticks a set of modules. The user can still change every tick.
struct Preset {
    std::string id, title, blurb;
    std::vector<std::string> modules;   // before requirements are added
};

inline const std::vector<Preset>& Presets() {
    static const std::vector<Preset> p = {
        {"2d", "2D game", "Sprites, tilemaps, 2D physics and characters, UI and audio.",
         {"physics2d", "tilemap2d", "controllers_2d", "particles", "audio", "ui_kit", "save_games",
          "scripting_okayscript", "scripting_visual", "cutscenes", "animation"}},
        {"3d_action", "3D action", "3D characters, weapons, health, enemies, interaction and effects.",
         {"physics3d", "animation", "ik", "ragdoll", "character_controllers", "health_damage", "weapons", "interaction",
          "ai_navigation", "ai_behavior", "audio", "ui_kit", "particles", "render3d_effects", "sky_weather", "cutscenes",
          "scripting_okayscript", "scripting_visual", "save_games"}},
        {"survival", "Survival", "Everything in 3D action plus hunger and thirst, inventory, crafting, building and terrain.",
         {"physics3d", "animation", "ik", "ragdoll", "character_controllers", "health_damage", "weapons", "interaction",
          "ai_navigation", "ai_behavior", "audio", "ui_kit", "particles", "render3d_effects", "sky_weather", "cutscenes",
          "scripting_okayscript", "scripting_visual", "save_games", "survival", "inventory", "crafting", "building",
          "terrain_foliage", "dialogue_quests", "vehicles"}},
        {"full", "Everything", "Every module. Nothing is left out of the game.", {}},   // empty = all, see ApplyPreset
    };
    return p;
}

/// One line of the checklist.
struct Row {
    std::string name, title, area, description;
    std::vector<std::string> requires_;     // what it needs directly
    bool on = false;
    bool core = false;
    bool locked = false;                    // cannot be turned off now
    std::string lockReason;                 // "needed by: weapons, vehicles" / "always on"
    int sizeKB = 0;                         // 0 = not known
};

class Selection {
public:
    /// `text` is the project's saved list ("" = every module, the way old projects work).
    explicit Selection(const ModuleRegistry& reg, const std::string& text = std::string()) : reg_(&reg) { Load(text); }

    void Load(const std::string& text) {
        on_.clear();
        if (text.empty()) { ApplyAll(); return; }
        std::vector<std::string> unknown;
        for (auto& n : reg_->Resolve(okay::ModuleManifest::SplitList(text), nullptr, &unknown)) on_.insert(n);
        unknown_ = unknown;   // modules a newer editor knew: kept visible, never dropped silently
    }
    bool IsOn(const std::string& name) const { return on_.count(name) != 0; }
    const std::vector<std::string>& Unknown() const { return unknown_; }

    struct Change {
        bool ok = true;
        std::vector<std::string> alsoOn;    // turned on because the module needs them
        std::vector<std::string> blockedBy; // enabled modules that need it (when turning off was refused)
    };
    /// Turn a module on (adding what it needs) or off (refused while an enabled module needs it, and for core).
    Change Set(const std::string& name, bool on) {
        Change c;
        const auto* info = reg_->Find(name);
        if (!info) { c.ok = false; return c; }
        if (on) {
            for (auto& r : reg_->TransitiveRequires(name))
                if (reg_->Find(r) && !IsOn(r)) { on_.insert(r); c.alsoOn.push_back(r); }
            on_.insert(name);
            return c;
        }
        if (info->manifest.core) { c.ok = false; return c; }
        for (auto& by : reg_->RequiredBy(name))
            if (IsOn(by)) c.blockedBy.push_back(by);
        if (!c.blockedBy.empty()) { c.ok = false; return c; }
        on_.erase(name);
        return c;
    }
    /// Replace the selection with a preset ("full" = everything).
    bool ApplyPreset(const std::string& id) {
        for (auto& p : Presets()) {
            if (p.id != id) continue;
            on_.clear();
            if (p.modules.empty()) { ApplyAll(); return true; }
            for (auto& n : reg_->Resolve(p.modules)) on_.insert(n);
            return true;
        }
        return false;
    }
    /// The preset whose set equals the selection, or "" (shown as "custom").
    std::string MatchingPreset() const {
        for (auto& p : Presets()) {
            Selection other(*reg_);
            other.ApplyPreset(p.id);
            if (other.on_ == on_) return p.id;
        }
        return "";
    }
    /// For project.okayproj: "" when every module is on (so such a project looks like an old one and keeps
    /// following the engine), else the modules in dependency order, core left out (it is always on).
    std::string ToText() const {
        if (AllOn()) return "";
        std::string out;
        for (auto& n : reg_->DependencyOrder()) {
            const auto* i = reg_->Find(n);
            if (!on_.count(n) || (i && i->manifest.core)) continue;
            if (!out.empty()) out += ",";
            out += n;
        }
        for (auto& u : unknown_) { if (!out.empty()) out += ","; out += u; }
        return out;
    }
    bool AllOn() const {
        for (auto& e : reg_->All()) if (!on_.count(e.manifest.name)) return false;
        return unknown_.empty();
    }
    std::vector<std::string> OnList() const {
        std::vector<std::string> out;
        for (auto& n : reg_->DependencyOrder()) if (on_.count(n)) out.push_back(n);
        return out;
    }
    /// Rows for the checklist, grouped by area then title. `sizeKB` comes from `sizes` (module -> KB) when given.
    std::vector<Row> Rows(const std::map<std::string, int>* sizes = nullptr) const {
        std::vector<Row> rows;
        for (auto& e : reg_->All()) {
            const auto& m = e.manifest;
            if (m.core) continue;     // core goes first, below
            Row r;
            r.name = m.name; r.title = m.title; r.area = m.area; r.description = m.description;
            r.requires_ = m.requires_;
            r.requires_.erase(std::remove_if(r.requires_.begin(), r.requires_.end(), [&](const std::string& q) {
                const auto* qi = reg_->Find(q); return qi && qi->manifest.core; }), r.requires_.end());
            r.on = IsOn(m.name);
            std::vector<std::string> by;
            for (auto& b : reg_->RequiredBy(m.name)) if (IsOn(b)) by.push_back(b);
            if (r.on && !by.empty()) {
                r.locked = true;
                r.lockReason = "needed by: ";
                for (size_t i = 0; i < by.size(); ++i) r.lockReason += (i ? ", " : "") + by[i];
            }
            if (sizes) { auto it = sizes->find(m.name); if (it != sizes->end()) r.sizeKB = it->second; }
            rows.push_back(r);
        }
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            return a.area != b.area ? a.area < b.area : a.title < b.title; });
        // The core is always on: shown first, ticked and greyed, with the reason as its tooltip.
        for (auto& e : reg_->All()) {
            if (!e.manifest.core) continue;
            Row r;
            r.name = e.manifest.name; r.title = e.manifest.title; r.area = "Always on";
            r.description = e.manifest.description; r.on = true; r.core = true; r.locked = true;
            r.lockReason = "Always on: every other module is built on the core, so it cannot be turned off.";
            rows.insert(rows.begin(), r);
        }
        return rows;
    }
    /// "also needs: Health and damage, Interaction" for a row (titles, not ids).
    std::string NeedsText(const Row& r) const {
        std::string s;
        for (auto& q : reg_->TransitiveRequires(r.name)) {
            const auto* i = reg_->Find(q);
            if (!i || i->manifest.core) continue;
            s += (s.empty() ? "" : ", ") + i->manifest.title;
        }
        return s;
    }

private:
    void ApplyAll() { for (auto& e : reg_->All()) on_.insert(e.manifest.name); }
    const ModuleRegistry* reg_;
    std::set<std::string> on_;
    std::vector<std::string> unknown_;
};

/// What the open scene uses: per module, how many objects have one of its components.
inline std::map<std::string, int> UsageInScene(const ModuleRegistry& reg, const okay::Scene& scene) {
    std::map<std::string, int> out;
    for (const auto& up : scene.Objects()) {
        if (!up) continue;
        std::set<std::string> seen;
        for (const auto& c : up->Components()) {
            if (!c) continue;
            const auto* m = reg.ModuleOfComponent(okay::ComponentSerializerRegistry::TypeKey(*c));
            if (m) seen.insert(m->manifest.name);
        }
        for (auto& n : seen) ++out[n];
    }
    return out;
}

/// Modules that are off but the scene uses: what the export would drop (the Problems panel lists these).
inline std::vector<std::pair<std::string, int>> UsedButOff(const Selection& sel, const std::map<std::string, int>& usage) {
    std::vector<std::pair<std::string, int>> out;
    for (auto& kv : usage) if (!sel.IsOn(kv.first)) out.push_back(kv);
    return out;
}

} // namespace okaymod
