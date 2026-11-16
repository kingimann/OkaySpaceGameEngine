#pragma once
// A Guided human with a skeleton and skin weights, as objects in a scene: the animation system can then drive it like any
// imported humanoid (see Render/ModelImport.hpp AttachCharacterModelRoot, and Components/HumanoidRetarget.hpp).
#include "okay/Modeling/Guided.hpp"
#include "okay/Scene/Scene.hpp"
#include <string>

namespace okay::modeling::guided {

/// Build the model from `recipe` (a "human") as a skinned character: a root object with a "Body" object (MeshRenderer +
/// SkinnedMesh) and the 15 bone objects from HumanoidSkeleton() as a hierarchy, weights from SkinVertices(). The root is not
/// parented to anything. Returns null with `why` set when the recipe has no skeleton.
GameObject* BuildRiggedHuman(Scene& scene, const Recipe& recipe, const std::string& name, std::string* why = nullptr);

} // namespace okay::modeling::guided
