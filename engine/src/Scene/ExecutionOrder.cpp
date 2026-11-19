#include "okay/Scene/ExecutionOrder.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include <algorithm>
#include <map>
#include <typeindex>
#include <unordered_map>

namespace okay {

namespace {
std::map<std::string, RunWhen>& Rules() { static std::map<std::string, RunWhen> m; return m; }
// type -> the value it runs at, worked out once per type after each change of the rules.
std::unordered_map<std::type_index, int>& Cache() { static std::unordered_map<std::type_index, int> m; return m; }
std::uint64_t g_version = 1;
}

bool ExecutionOrderSettings::Set(const std::string& typeName, RunWhen when) {
    if (typeName.empty()) return false;
    for (char c : typeName) if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t') return false;
    if (when == RunWhen::Normal) Rules().erase(typeName);
    else Rules()[typeName] = when;
    Cache().clear();
    ++g_version;
    return true;
}

RunWhen ExecutionOrderSettings::Get(const std::string& typeName) {
    auto it = Rules().find(typeName);
    return it == Rules().end() ? RunWhen::Normal : it->second;
}

std::vector<std::pair<std::string, RunWhen>> ExecutionOrderSettings::All() {
    return std::vector<std::pair<std::string, RunWhen>>(Rules().begin(), Rules().end());
}

void ExecutionOrderSettings::Clear() {
    if (Rules().empty()) return;
    Rules().clear();
    Cache().clear();
    ++g_version;
}

bool ExecutionOrderSettings::Any() { return !Rules().empty(); }
std::uint64_t ExecutionOrderSettings::Version() { return g_version; }

int ExecutionOrderSettings::Effective(const Component& c) {
    if (Rules().empty()) return c.ExecutionOrder();
    const std::type_index t(typeid(c));
    auto it = Cache().find(t);
    if (it != Cache().end()) return it->second == 0 ? c.ExecutionOrder() : it->second;
    // A type is "Early"/"Late" by its own name, or by a base class's name when the rule names a base.
    const RunWhen w = Get(ComponentSerializerRegistry::TypeKey(c));
    const int v = w == RunWhen::Early ? kEarly : w == RunWhen::Late ? kLate : 0;
    Cache()[t] = v;
    return v == 0 ? c.ExecutionOrder() : v;
}

const char* ExecutionOrderSettings::Label(RunWhen when) {
    return when == RunWhen::Early ? "Run early" : when == RunWhen::Late ? "Run late" : "Normal";
}

} // namespace okay
