// Private to engine/src/Components/Character*.cpp: the mesh helper that Character.cpp and CharacterMotion.cpp both call.
// Not installed, not for other folders.
#pragma once
#include "okay/Render/Mesh.hpp"

namespace okay {

namespace character_rig {
// Mirror a baked block-body mesh from +Z onto -Z and swap each triangle so the faces point outward again.
void FaceMinusZ(Mesh& m);
} // namespace character_rig

} // namespace okay
