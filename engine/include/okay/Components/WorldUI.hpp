#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec3.hpp"
#include <string>

namespace okay {

/// In-world (3D) UI: a text label with an optional background panel, anchored to a
/// point in the 3D world (this object's position + worldOffset) and drawn at its
/// projected screen location. The classic uses are nameplates, health/▮▮▮ bars,
/// damage numbers, quest markers and interaction prompts that float over objects.
/// The host (player / editor) projects the point with the main camera
/// (Camera::WorldToScreen) and draws the label, scaling it with distance.
class WorldUI : public Behaviour {
public:
    std::string text = "Label";
    Color color = Color::White;                 // text color
    Color background = Color::FromBytes(0, 0, 0, 0);  // alpha 0 => no panel behind
    Vec3  worldOffset{0.0f, 2.0f, 0.0f};        // offset from the object (default: above it)
    float pixelSize = 2.0f;                     // base font pixel size at the reference distance
    bool  scaleWithDistance = true;             // shrink as it gets farther from the camera
    float refDistance = 8.0f;                   // distance at which pixelSize is exact
    float minScale = 0.4f, maxScale = 2.5f;     // clamp for distance scaling
    float maxDistance = 0.0f;                    // 0 = always draw; else hide beyond this
    bool  hideBehind = true;                    // hide when behind the camera

    /// 0..1 fill for a quick health/progress bar drawn under the text. <0 disables
    /// the bar (the default), so a plain label draws no bar.
    float bar = -1.0f;
    Color barColor = Color::FromBytes(90, 200, 120);
    Color barBackground = Color::FromBytes(30, 34, 44, 200);
};

} // namespace okay
