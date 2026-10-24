#pragma once
// Variables shared beyond one graph: scene variables (every graph in the same
// scene sees them; they go when the scene does) and global variables (they
// survive loading another scene; the editor clears them when Play starts).
#include "okay/VisualScript/VsValue.hpp"
#include <string>
#include <unordered_map>

namespace okay { class Scene; }

namespace okay::vs {

using VarMap = std::unordered_map<std::string, VsValue>;

/// The variables shared by every visual script in `scene`.
VarMap& SceneVariables(const Scene* scene);
/// Drop `scene`'s variables (its destructor calls this).
void ForgetSceneVariables(const Scene* scene);
/// Variables that last for the whole game run, across scene loads.
VarMap& GlobalVariables();
void ClearGlobalVariables();

/// Whether two values are the same for "did it change?" purposes: same kind and
/// equal value; objects by identity; lists / maps by identity (so adding to a
/// list isn't a change of the variable holding it, setting another list is).
bool VsSameValue(const VsValue& a, const VsValue& b);

} // namespace okay::vs
