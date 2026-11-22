#pragma once
// ModelSource: kept on the top object of a model placed in a scene from a model file (.glb, .gltf, .fbx, .obj ...).
// It remembers which file the model came from, the import Scale it was given and which objects under it are the
// model's own parts, so the editor can bring the placed model up to date when the file changes on disk, is
// reimported, or gets new import settings (see okay/Render/AssetRefresh.hpp). Data only: it does nothing at run time.
// Saved in the scene as   modelsource "<path>" <appliedScale> <count> "<part>" "<part>" ...
#include "okay/Scene/Component.hpp"

#include <string>
#include <vector>

namespace okay {

class ModelSource : public Component {
public:
    /// The model file, as the editor names files (relative to the project folder when it is inside it).
    std::string path;
    /// The import setting "Scale" that was multiplied into the top object's size when it was placed or last
    /// refreshed. A new Scale changes the size by new / old, so a size you set by hand is kept.
    float appliedScale = 1.0f;
    /// The model's own parts at the last import, as paths below the top object ("Root/Arm", "Root/Arm#1" for the
    /// second sibling of that name; see ModelNodePath). An object under the model that is not listed is yours.
    std::vector<std::string> nodes;

    bool HasNode(const std::string& node) const {
        for (const std::string& n : nodes) if (n == node) return true;
        return false;
    }
};

} // namespace okay
