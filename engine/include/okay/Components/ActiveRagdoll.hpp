#pragma once
// ActiveRagdoll is the old name of the one Ragdoll component (Components/Ragdoll.hpp), kept so code written
// against it still builds. Scenes saved with it load into a Ragdoll.
#include "okay/Components/Ragdoll.hpp"

namespace okay {
using ActiveRagdoll = Ragdoll;
} // namespace okay
