#pragma once
// Marks a mesh as an occlusion caster (feature map 27.2). The visibility pass
// rasterizes it into the CPU occlusion buffer. Without this component nothing
// is occlusion-culled, so existing scenes draw exactly as before.
#include "okay/Scene/Component.hpp"

namespace okay {

class Occluder : public Component {
public:
    /// Rasterize the mesh. Off: rasterize the world bounding box instead (a
    /// solid block — only right for a real wall, not a ground plane).
    bool useMesh = true;
};

} // namespace okay
