#pragma once
// "Open in safe mode": when a scene file will not load as a whole (the loader is all-or-nothing on purpose), this
// reads it object by object and keeps every object that loads on its own. The damaged ones are listed, never
// guessed at. The result is text for a NEW, unsaved scene: the broken file is never written to.
//
// Written from the scene text format (docs/scene_serializer.md): a header and scene-wide records, then one
// `gameobject <index> "<name>"` record per object, each ending at the next one.
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <string>
#include <vector>

namespace okay::editor {

struct SalvageResult {
    bool usable = false;                 ///< the header and at least one object were readable
    std::string text;                    ///< the scene text to open (header + the objects that loaded)
    int total = 0;                       ///< objects found in the file
    int kept = 0;                        ///< objects that loaded on their own
    std::vector<std::string> dropped;    ///< "Door (object 12): <why>" for each one left out
    std::string headerError;             ///< why the header itself was unreadable
};

inline SalvageResult SalvageSceneText(const std::string& text) {
    SalvageResult r;
    // Cut into the header and one block per object: a block starts at a line that begins with "gameobject ".
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < text.size();) {
        if (text.compare(i, 11, "gameobject ") == 0) starts.push_back(i);
        const std::size_t e = text.find('\n', i);
        if (e == std::string::npos) break;
        i = e + 1;
    }
    const std::string header = text.substr(0, starts.empty() ? text.size() : starts.front());
    r.total = (int)starts.size();
    {
        Scene probe;
        std::string err;
        if (!SceneSerializer::Deserialize(probe, header.empty() || header.back() == '\n' ? header : header + "\n", &err)) {
            r.headerError = err;
            return r;
        }
    }
    r.text = header.empty() || header.back() == '\n' ? header : header + "\n";
    for (std::size_t b = 0; b < starts.size(); ++b) {
        const std::size_t end = b + 1 < starts.size() ? starts[b + 1] : text.size();
        std::string block = text.substr(starts[b], end - starts[b]);
        if (block.empty() || block.back() != '\n') block += '\n';
        // The label for the list: the object's name and its number from the first line.
        std::string label = "object " + std::to_string(b);
        {
            const std::size_t nl = block.find('\n');
            const std::string first = block.substr(0, nl);
            const std::size_t q1 = first.find('"'), q2 = first.rfind('"');
            if (q1 != std::string::npos && q2 > q1) label = first.substr(q1 + 1, q2 - q1 - 1) + " (" + first.substr(0, q1 - (q1 > 0 && first[q1 - 1] == ' ' ? 1 : 0)) + ")";
        }
        Scene one;
        std::string err;
        const bool ok = SceneSerializer::Deserialize(one, header + (header.empty() || header.back() == '\n' ? "" : "\n") + block, &err);
        if (ok) { r.text += block; ++r.kept; }
        else r.dropped.push_back(label + ": " + err);
    }
    // The pieces that loaded one at a time must also load together (a child may point at a dropped parent).
    {
        Scene all;
        std::string err;
        if (r.kept > 0 && !SceneSerializer::Deserialize(all, r.text, &err)) {
            r.headerError = "the readable objects do not load together: " + err;
            r.kept = 0;
            return r;
        }
    }
    r.usable = r.kept > 0;
    return r;
}

}  // namespace okay::editor
