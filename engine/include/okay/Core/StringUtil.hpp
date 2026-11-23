#pragma once
// The engine's one place for small text helpers (CLAUDE.md rule 19c, docs/engine/shared_code.md "string helpers").
// Add to this file instead of writing another Trim/Lower/Split next to your code. Header-only, std only.
// Written from scratch for this engine.
#include <string>

namespace okay::str {

// ASCII-only lowercase: bytes 'A'..'Z' become 'a'..'z', every other byte (UTF-8 included) is left as it is.
// Locale-independent, unlike std::tolower with a non-"C" locale.
inline std::string LowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

}  // namespace okay::str
