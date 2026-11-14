#pragma once
#include "okay/AI/NavMesh.hpp"
#include "okay/Scene/Component.hpp"

namespace okay {

/// Marks the navigation area of this object's colliders when the NavMeshSurface bakes:
/// a swamp is Water (slow to cross), a path is Road (cheap), a pit or a lava pool is
/// NotWalkable (a hole in the mesh), and areas 4..31 are the project's own. The area
/// is stored on each baked polygon, so bake again after changing it; what an area
/// costs and who may enter it is set on the surface and on each NavMeshAgent.
class NavMeshModifier : public Component {
public:
    int  area = NavArea::Walkable;   ///< NavArea 0..31
    bool applyToChildren = true;     ///< also colliders on child objects that carry no modifier of their own
};

} // namespace okay
