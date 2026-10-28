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
};

} // namespace okay
