#pragma once
// Scenes under examples/node_playground, one file per group (flow.okayscene, ...).
// File > Examples > Node Playground lists every .okayscene there. Another group
// drops a scene in that folder and it shows up in the same menu.
#include "ExamplesRoot.hpp"
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace okay::editor {

inline std::vector<std::string> NodePlaygroundScenes() { return ExampleScenes("node_playground"); }

} // namespace okay::editor
