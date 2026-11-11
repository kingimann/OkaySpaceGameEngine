#pragma once
// Scenes under examples/node_playground, one file per group (flow.okayscene, ...).
// File > Examples > Node Playground lists every .okayscene there. Another group
// drops a scene in that folder and it shows up in the same menu.
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace okay::editor {

inline std::vector<std::string> NodePlaygroundScenes() {
    namespace fs = std::filesystem;
    const fs::path roots[] = {
        "examples/node_playground",
        "../examples/node_playground",
        "../../examples/node_playground",
    };
    std::vector<std::string> out;
    for (const fs::path& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (const auto& f : fs::directory_iterator(root, ec)) {
            if (ec) break;
            if (f.path().extension() == ".okayscene")
                out.push_back(f.path().lexically_normal().string());
        }
        if (!out.empty()) break;
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace okay::editor
