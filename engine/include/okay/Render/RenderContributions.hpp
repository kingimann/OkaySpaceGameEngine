#pragma once
// A module adds a debug-overlay line (and, later, a draw pass) without the renderer
// naming that module. DebugOverlay::BuildLines appends these lines. With the module
// off the line is left out. Written for this engine from the module plan
// (docs/design/modular_gameplay_contracts.md, section 9, PR 11).
#include <functional>
#include <string>
#include <vector>

namespace okay {

class Scene;

struct RenderContribution {
    std::string name;
    std::string module;   ///< empty = always on
    /// One overlay line, or "" when there is nothing to say. `scene` may be null.
    std::function<std::string(const Scene*)> overlay;
};

void AddRenderContribution(RenderContribution contribution);

/// Lines from contributions whose module is on, in registration order. Empty strings are dropped.
std::vector<std::string> RenderContributionLines(const Scene* scene);

} // namespace okay
