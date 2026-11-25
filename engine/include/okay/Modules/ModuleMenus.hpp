#pragma once
// Add Component rows and scene templates built from the registries, so a module that is
// off does not offer its component or its template. editor/main.cpp is not edited
// (#1192 is open); editor/AddComponentFromRegistry.hpp is the hook the inspector can call.
#include "okay/Scene/Scene.hpp"
#include <string>
#include <vector>

namespace okay {

struct AddComponentRow {
    std::string type;       ///< PropertyRegistry type name ("Crosshair")
    std::string record;     ///< scene record name
    std::string category;
    std::string module;     ///< empty when no module claims the type
};

/// Every component the property table can add, minus ones whose module is off.
std::vector<AddComponentRow> AddComponentRows();

struct SceneTemplateEntry {
    std::string id;
    std::string module;
    void (*build)(Scene& scene) = nullptr;
};

void RegisterSceneTemplate(SceneTemplateEntry entry);

/// The template, or null when it is missing or its module is off.
const SceneTemplateEntry* FindSceneTemplate(const std::string& id);

struct ModuleAssetNote {
    std::string component, field, kind, path;
};

/// A module adds the asset paths its components store, for the scene validator.
void AddModuleAssetScan(std::string module, void (*scan)(const GameObject&, std::vector<ModuleAssetNote>&));

void CollectModuleAssets(const GameObject& go, std::vector<ModuleAssetNote>& out);

} // namespace okay
