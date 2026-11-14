#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"

namespace okay {

/// A rigidbody's last two physics poses and the pose it is drawn at, for render
/// interpolation (Scene::interpolatePhysics). Runtime only; kept by the Scene.
struct PoseHistory {
    Vec3 prevPos, curPos, shownPos;
    Quat prevRot = Quat::Identity, curRot = Quat::Identity, shownRot = Quat::Identity;
    bool valid = false;   ///< false until the body has been stepped, or after it was moved by hand
    /// Local pose at the start of the last step, and the real physics local pose.
    /// A moving parent carries these; the drawn pose is only the in-between one.
    Vec3 prevLocal, physLocal, shownLocal;
    Quat prevLocalRot = Quat::Identity, physLocalRot = Quat::Identity, shownLocalRot = Quat::Identity;
    bool shownLocalValid = false;
    bool prevHadParent = false;
};

} // namespace okay
