// Private to engine/src/Components/Character*.cpp: the one helper of the first-person arm code (CharacterFirstPerson.cpp)
// that Character.cpp also calls. Not installed, not for other folders.
#pragma once

namespace okay {

class Camera;
class GameObject;

namespace character_fp {
// The camera that is allowed to draw this player's first-person-only arm (null when there is none).
Camera* OwnerCamera(GameObject* player);
} // namespace character_fp

} // namespace okay
