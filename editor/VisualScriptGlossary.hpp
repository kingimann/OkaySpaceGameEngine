#pragma once
// The plain-words rule for visual-script names and descriptions: "imagine a 10 year old was using it".
// This header holds the list of programming words a display name or one-line description may not use,
// a small allow-list for the few unavoidable terms (each one justified), and the check itself.
// Written from the project's own naming notes (docs/visual_script_category_audit.md, glossary section).
// No ImGui and no threads, so the editor, the tests and the player can all include it.
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace okay::editor::vsed {

// Words a beginner does not know. Matched as whole lower-case words (a trailing "s" is also caught).
inline const char* const kVsProgrammingWords[] = {
    "variable", "boolean", "bool", "float", "integer", "int", "vector", "vec", "vec2", "vec3", "instance",
    "callback", "handler", "parameter", "argument", "iterate", "iteration", "index", "string", "array",
    "function", "node", "nodes", "euler", "quaternion", "quat", "gameobject", "prefab", "transform",
    "rigidbody", "api", "enum", "struct", "null", "void", "const", "constant", "casting", "cast",
    "serialize", "deserialize", "json", "dt", "obj", "pos", "rot", "idx", "fps", "fov", "rpc", "lerp",
    "raycast", "scalar", "var", "vars", "anim", "num", "id", "ids", "nav", "ik", "min", "max", "deg", "dist", "tuple", "dictionary", "hash", "mutex", "thread", "loop",
};

// The few terms a game maker really does need. Each one is justified; add a row only with a reason.
struct VsGlossaryAllowed { const char* word; const char* why; };
inline const VsGlossaryAllowed kVsGlossaryAllowed[] = {
    {"loop", "'Loop' is the common word for 'repeat'; kept in names only where Repeat would clash (e.g. 'Loop Sound')"},
    {"index", "'Number in the list' is longer; kept in descriptions of list nodes only"},
    {"constant", "a fixed number; the node is called Fixed Number, the word only appears in older search text"},
    {"transform", "the Inspector section is still called Transform"},
};

inline bool VsGlossaryIsAllowed(const std::string& low) {
    for (const auto& a : kVsGlossaryAllowed) if (low == a.word) return true;
    return false;
}

inline std::vector<std::string> VsGlossaryWords(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (std::isalnum((unsigned char)c)) cur += (char)std::tolower((unsigned char)c);
        else if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

/// Empty when the text is in plain words. Otherwise the first banned word found.
/// allowAllowListed: description text may use the justified allow-list words.
inline std::string VsGlossaryViolation(const std::string& text, bool allowAllowListed = false) {
    for (const std::string& w : VsGlossaryWords(text)) {
        std::string stem = w;
        if (stem.size() > 3 && stem.back() == 's') stem.pop_back();
        for (const char* b : kVsProgrammingWords) {
            if (w == b || stem == b) {
                if (allowAllowListed && VsGlossaryIsAllowed(w)) break;
                return w;
            }
        }
    }
    return {};
}

} // namespace okay::editor::vsed
