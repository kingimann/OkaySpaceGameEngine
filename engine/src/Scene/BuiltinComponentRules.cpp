#include "okay/Scene/BuiltinComponentRules.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Components/Joint2D.hpp"
#include "okay/Components/Joint3D.hpp"
#include "okay/Components/Buoyancy.hpp"
#include "okay/Components/BoatController.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UIInputValidator.hpp"

namespace okay {

// Only pairs where the first component does nothing at all without the second: a joint moves THIS object's body
// (Physics3D/Physics2D skip a joint whose object has none), Buoyancy and the Boat Controller push a Rigidbody 3D, and the
// validator checks the Input Field next to it. AddRequirement ignores a pair it already has, so calling this twice is safe.
// The scene loader does not apply the rules (it keeps what the file says), so an old scene with a lone joint stays as it was.
void RegisterBuiltinComponentRules() {
    RequireComponent<Joint3D, Rigidbody3D>();
    RequireComponent<Joint2D, Rigidbody2D>();
    RequireComponent<Buoyancy, Rigidbody3D>();
    RequireComponent<BoatController, Rigidbody3D>();
    RequireComponent<UIInputValidator, UIInputField>();
}

} // namespace okay
