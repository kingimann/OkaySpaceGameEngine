#include "okay/Scripting/ScriptApiRegistry.hpp"
#include "okay/Modules/ModuleRegistry.hpp"

namespace okay {

namespace {
std::vector<std::pair<std::string, ScriptApiFn>>& Table() { static std::vector<std::pair<std::string, ScriptApiFn>> t; return t; }
}

void AddScriptApi(const std::string& name, ScriptApiFn fn) {
    for (auto& e : Table()) if (e.first == name) { e.second = std::move(fn); return; }
    Table().emplace_back(name, std::move(fn));
}

const std::vector<std::pair<std::string, ScriptApiFn>>& ScriptApis() {
    (void)ModuleRegistry::Get();
    return Table();
}

} // namespace okay
