#pragma once
// A camera zone: a region that changes how a camera rig frames things while its target is inside, and blends
// the change in across the edge (field of view, where the camera stands, how snappy it is). Walk into a tight
// corridor and the view pulls in and widens; walk out and it eases back. It reuses the Volume region shape
// (`VolumeGeometry`) and its edge fade shape; the rig reads it in VirtualCamera::Solve. docs/camera_upgrade.md.
#include "okay/Components/Volume.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include <string>

namespace okay {

class CameraZone : public Behaviour {
public:
    VolumeShape shape = VolumeShape::Box;
    Vec3 size{8.0f, 4.0f, 8.0f};           ///< the region, in local units
    Vec3 offset{0.0f, 0.0f, 0.0f};         ///< its centre, in local units
    float blend = 0.35f;                   ///< how far in from the edge it takes to reach full effect (0 = at once), 0..1
    int   priority = 0;                    ///< a zone with a higher number is applied after (over) a lower one
    std::string shot;                      ///< only this shot (a Virtual Camera's object name); blank = every shot

    float fieldOfView = 0.0f;              ///< the field of view inside, degrees (0 = leave it alone)
    Vec3  followOffset{0.0f, 0.0f, 0.0f};  ///< added to the shot's Follow Offset inside
    float positionDamping = -1.0f;         ///< the damping rate inside (-1 = leave it alone; higher is snappier)

    VolumeGeometry Geometry() const { return {shape, size, offset}; }
    /// 0 outside, 1 at full effect inside; eased across `blend`.
    float WeightAt(const Vec3& world) const {
        if (!transform || !Geometry().Valid()) return 0.0f;
        const float d = Geometry().DepthAt(*transform, world);
        if (!(d >= 0.0f)) return 0.0f;
        if (blend <= 1e-4f) return 1.0f;
        float t = d / blend; t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return t * t * (3.0f - 2.0f * t);
    }
};

} // namespace okay
