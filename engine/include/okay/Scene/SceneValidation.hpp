#pragma once
#include <string>
#include <vector>

namespace okay {

/// One problem found in a scene by SceneSerializer::Validate / Scene::Validate.
struct SceneIssue {
    enum class Kind {
        MissingComponentType,   // an object has a component record this build does not know
        UnknownField,           // a top-level record this build does not know
        MissingAsset,           // a texture / mesh / audio / prefab ... file that does not exist
        BrokenParentLink,       // `parent N` names an object that is not in the file
        DuplicateGuid,          // two objects carry the same `guid`
        LoadError               // the text could not be loaded at all (see message)
    };
    Kind kind = Kind::LoadError;
    std::string object;    // "Name (#index)" of the object concerned, empty for scene-level issues
    std::string message;
};

/// The result of validating a scene. Empty `issues` means the scene is clean.
struct SceneValidationReport {
    std::vector<SceneIssue> issues;
    bool Ok() const { return issues.empty(); }
    int Count(SceneIssue::Kind kind) const {
        int n = 0;
        for (const auto& i : issues) if (i.kind == kind) ++n;
        return n;
    }
    /// One line per issue, for logs and the editor console.
    std::string ToString() const {
        std::string s;
        for (const auto& i : issues) {
            static const char* names[] = {"missing component type", "unknown field", "missing asset",
                                          "broken parent link", "duplicate guid", "load error"};
            s += names[static_cast<int>(i.kind)];
            if (!i.object.empty()) s += " [" + i.object + "]";
            s += ": " + i.message + "\n";
        }
        return s;
    }
};

} // namespace okay
