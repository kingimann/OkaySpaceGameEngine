#include "okay/Modules/ModuleMenus.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/PropertyRegistry.hpp"

namespace okay {

std::vector<AddComponentRow> AddComponentRows() {
    ModuleRegistry::Get();
    RegisterBuiltinProperties();
    std::vector<AddComponentRow> out;
    for (const auto& info : PropertyRegistry::All()) {
        if (!info || !info->add) continue;
        std::string module;
        if (const ModuleRegistry::Info* owner = ModuleRegistry::Get().ModuleOfComponent(info->name)) {
            module = owner->manifest.name;
            if (!owner->enabled) continue;
        }
        out.push_back({info->name, info->recordName, info->category, module});
    }
    return out;
}

namespace {
std::vector<SceneTemplateEntry>& Templates() {
    static std::vector<SceneTemplateEntry> t;
    return t;
}
struct AssetScan { std::string module; void (*scan)(const GameObject&, std::vector<ModuleAssetNote>&); };
std::vector<AssetScan>& Scans() {
    static std::vector<AssetScan> s;
    return s;
}
}

void RegisterSceneTemplate(SceneTemplateEntry entry) {
    for (SceneTemplateEntry& e : Templates()) {
        if (e.id == entry.id) { e = entry; return; }
    }
    Templates().push_back(entry);
}

const SceneTemplateEntry* FindSceneTemplate(const std::string& id) {
    ModuleRegistry::Get();
    for (const SceneTemplateEntry& e : Templates()) {
        if (e.id != id) continue;
        if (!e.module.empty() && !ModuleRegistry::Get().IsEnabled(e.module)) return nullptr;
        return &e;
    }
    return nullptr;
}

void AddModuleAssetScan(std::string module, void (*scan)(const GameObject&, std::vector<ModuleAssetNote>&)) {
    if (!scan) return;
    for (AssetScan& s : Scans()) if (s.module == module) { s.scan = scan; return; }
    Scans().push_back({std::move(module), scan});
}

void CollectModuleAssets(const GameObject& go, std::vector<ModuleAssetNote>& out) {
    ModuleRegistry::Get();
    for (const AssetScan& s : Scans()) {
        if (!s.module.empty() && !ModuleRegistry::Get().IsEnabled(s.module)) continue;
        s.scan(go, out);
    }
}

} // namespace okay
