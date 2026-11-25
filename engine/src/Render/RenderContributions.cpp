#include "okay/Render/RenderContributions.hpp"
#include "okay/Modules/ModuleRegistry.hpp"

namespace okay {

namespace {
std::vector<RenderContribution>& Table() {
    static std::vector<RenderContribution> t;
    return t;
}
}

void AddRenderContribution(RenderContribution contribution) {
    if (!contribution.overlay) return;
    for (RenderContribution& c : Table()) {
        if (c.name == contribution.name) { c = std::move(contribution); return; }
    }
    Table().push_back(std::move(contribution));
}

std::vector<std::string> RenderContributionLines(const Scene* scene) {
    ModuleRegistry::Get();
    std::vector<std::string> out;
    for (const RenderContribution& c : Table()) {
        if (!c.module.empty() && !ModuleRegistry::Get().IsEnabled(c.module)) continue;
        std::string line = c.overlay(scene);
        if (!line.empty()) out.push_back(std::move(line));
    }
    return out;
}

} // namespace okay
