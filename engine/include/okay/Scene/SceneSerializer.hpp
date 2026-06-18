#pragma once
#include <string>

namespace okay {

class Scene;

/// Saves and loads a Scene to a simple, human-readable text format. Supports the
/// built-in component types (Transform, SpriteRenderer, Camera) and preserves
/// the parent/child hierarchy. Used by the editor's File menu, but usable from
/// gameplay code too (level files, save games).
class SceneSerializer {
public:
    /// Serialize an entire scene to a string.
    static std::string Serialize(const Scene& scene);
    /// Write a scene to a file. Returns false on I/O error.
    static bool SaveToFile(const Scene& scene, const std::string& path);

    /// Rebuild `scene` (cleared first) from serialized text. Returns false on
    /// a parse error, with details in `error` when provided.
    static bool Deserialize(Scene& scene, const std::string& text, std::string* error = nullptr);
    /// Load a scene from a file.
    static bool LoadFromFile(Scene& scene, const std::string& path, std::string* error = nullptr);
};

} // namespace okay
