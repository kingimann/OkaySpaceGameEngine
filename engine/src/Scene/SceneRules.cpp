#include "okay/Scene/SceneRules.hpp"
#include "okay/Modules/ModuleRegistry.hpp"

namespace okay {
namespace rules {

namespace {
std::vector<SingletonRuleEntry>& Table() { static std::vector<SingletonRuleEntry> t; return t; }
}

void AddSingletonRule(const std::string& name, std::function<std::vector<Component*>(const Scene&)> find) {
    for (auto& e : Table()) if (e.name == name) { e.find = std::move(find); return; }
    Table().push_back({name, std::move(find)});
}

const std::vector<SingletonRuleEntry>& SingletonRules() {
    (void)ModuleRegistry::Get();
    return Table();
}

} // namespace rules
} // namespace okay
