#pragma once
// Old file tokens that still load. The title people see can change without a
// line here, because the token in the .okayvs file stays put. Add a line only
// when a token itself is retired (two nodes merged, or a token that was the
// only name). See docs/visual_scripting_naming.md.
#include <string>
#include <unordered_map>

namespace okay::vs {

inline std::string CanonicalNodeType(const std::string& type) {
    static const std::unordered_map<std::string, std::string> kNodeAliases = {
        // { "OldToken", "TokenThatStillExists" },
    };
    auto it = kNodeAliases.find(type);
    return it == kNodeAliases.end() ? type : it->second;
}

} // namespace okay::vs
