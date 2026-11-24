#include "okay/VisualScript/VsNodeRegistry.hpp"
#include "okay/VisualScript/NodesCombat.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include <utility>

namespace okay::vs {

namespace {
std::vector<VsNodeEntry>& Table() {
    static std::vector<VsNodeEntry> t;
    return t;
}
std::string Arg0(const std::vector<std::string>& args, const char* fallback) {
    return args.empty() || args[0].empty() ? std::string(fallback) : args[0];
}
std::unique_ptr<VsNode> MakeSwing(const std::vector<std::string>&, std::string*) {
    return std::make_unique<SwingWeaponNode>();
}
std::unique_ptr<VsNode> MakeFire(const std::vector<std::string>&, std::string*) {
    return std::make_unique<FireWeaponNode>();
}
std::unique_ptr<VsNode> MakeReload(const std::vector<std::string>&, std::string*) {
    return std::make_unique<ReloadWeaponNode>();
}
std::unique_ptr<VsNode> MakeAmmo(const std::vector<std::string>&, std::string*) {
    return std::make_unique<WeaponAmmoNode>();
}
std::unique_ptr<VsNode> MakeSetWeapon(const std::vector<std::string>& args, std::string*) {
    return std::make_unique<SetWeaponNode>(Arg0(args, "sword"));
}
} // namespace

void RegisterVsNode(std::string type, std::string module,
                    std::unique_ptr<VsNode> (*make)(const std::vector<std::string>&, std::string*)) {
    for (VsNodeEntry& e : Table()) {
        if (e.type == type) { e.module = std::move(module); e.make = make; return; }
    }
    Table().push_back(VsNodeEntry{std::move(type), std::move(module), make});
}

const VsNodeEntry* FindRegisteredVsNode(const std::string& type) {
    for (const VsNodeEntry& e : Table()) if (e.type == type) return &e;
    return nullptr;
}

bool VsNodeSuppressed(const std::string& type) {
    const VsNodeEntry* e = FindRegisteredVsNode(type);
    if (!e || e->module.empty()) return false;
    return !okay::ModuleRegistry::Get().IsEnabled(e->module);
}

std::vector<std::string> RegisteredVsNodeTypes(bool enabledOnly) {
    okay::ModuleRegistry::Get();
    std::vector<std::string> out;
    for (const VsNodeEntry& e : Table()) {
        if (enabledOnly && !e.module.empty() && !okay::ModuleRegistry::Get().IsEnabled(e.module)) continue;
        out.push_back(e.type);
    }
    return out;
}

void RegisterWeaponVsNodes() {
    RegisterVsNode("SwingWeapon", "weapons", &MakeSwing);
    RegisterVsNode("FireWeapon", "weapons", &MakeFire);
    RegisterVsNode("ReloadWeapon", "weapons", &MakeReload);
    RegisterVsNode("WeaponAmmo", "weapons", &MakeAmmo);
    RegisterVsNode("SetWeapon", "weapons", &MakeSetWeapon);
}

} // namespace okay::vs
