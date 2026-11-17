#pragma once
#include "okay/VisualScript/VsValue.hpp"
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace okay {

class GameObject;

/// A script function a module adds to OkayScript. The VM copies every registered function into its builtin table when it is
/// made, so a build without the module has no such function (a script that calls it gets the VM's usual "unknown function").
/// `self` is the object the script runs on (null for a script with no object); arguments and the result are the VM's own values.
using ScriptApiFn = std::function<vs::VsValue(GameObject* self, std::vector<vs::VsValue>& args)>;

void AddScriptApi(const std::string& name, ScriptApiFn fn);
/// Every registered function, in registration order. The first call lets the modules register.
const std::vector<std::pair<std::string, ScriptApiFn>>& ScriptApis();

} // namespace okay
