#pragma once
#include <functional>
#include <string>
#include <vector>

namespace okay {

class Component;
class Scene;

/// Scene check rules that modules add from their register.cpp, so the validator does not have to name their components.
namespace rules {

/// "There is only one of these in a scene" (a day-night cycle, a cinemachine brain...). `find` returns every such component in the
/// scene; more than one is a warning.
struct SingletonRuleEntry {
    std::string name;
    std::function<std::vector<Component*>(const Scene&)> find;
};
void AddSingletonRule(const std::string& componentName, std::function<std::vector<Component*>(const Scene&)> find);
/// The first call lets the modules register.
const std::vector<SingletonRuleEntry>& SingletonRules();

} // namespace rules
} // namespace okay
