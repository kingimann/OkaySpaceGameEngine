#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec2.hpp"
#include <cmath>

namespace okay {

/// The root of a screen-space UI, mirroring Unity's Canvas + CanvasScaler. A
/// scene's UI widgets are laid out in pixels; a Canvas decides how those pixels
/// map to the actual window so a HUD designed at one resolution stays sensible
/// at another.
///
///  - ConstantPixelSize: widgets stay the size you authored (1:1 pixels).
///  - ScaleWithScreenSize: widgets scale relative to a `referenceResolution`,
///    blending the width- and height-based scale by `matchWidthOrHeight`
///    (0 = follow width, 1 = follow height) — the same log2 blend Unity uses.
///
/// `sortOrder` lets multiple canvases stack (higher draws on top). The runtime
/// and editor query ScaleFactor() to size the UI for the current view.
class Canvas : public Behaviour {
public:
    enum class ScaleMode { ConstantPixelSize, ScaleWithScreenSize };

    ScaleMode scaleMode = ScaleMode::ConstantPixelSize;
    Vec2  referenceResolution{1280.0f, 720.0f};
    float matchWidthOrHeight = 0.5f;   // 0 = width, 1 = height
    float scaleFactor = 1.0f;          // extra user multiplier (applied in BOTH modes)
    int   sortOrder = 0;
    bool  visible = true;              // hide/show every widget under this canvas
    float opacity = 1.0f;             // 0..1 master fade for this canvas's widgets

    // ---- World-space mode (Unity's "World Space" render mode) ----
    // When true, this canvas lives on a plane in the 3D world (at its GameObject's
    // position) instead of the screen. Its widgets are authored in `designResolution`
    // pixels and projected through the active camera each frame, so buttons, panels,
    // images, sliders — every regular UI widget — render in-world (signs, terminals,
    // nameplates). `worldPixelsPerUnit` sets the size (design px per world unit) and
    // `billboard` keeps it facing the camera.
    bool  worldSpace = false;
    Vec2  designResolution{1280.0f, 720.0f};   // the canvas's own pixel space
    float worldPixelsPerUnit = 300.0f;         // design px per world unit (bigger = smaller in-world)
    bool  billboard = true;                    // always face the camera

    /// The pixel-scale this canvas applies for an actual screen of w x h. The
    /// user `scaleFactor` multiplies the result in both modes, so it always works
    /// as an extra UI zoom.
    float ScaleFactor(float screenW, float screenH) const {
        if (scaleMode == ScaleMode::ConstantPixelSize)
            return scaleFactor;
        float rw = referenceResolution.x > 1.0f ? referenceResolution.x : 1.0f;
        float rh = referenceResolution.y > 1.0f ? referenceResolution.y : 1.0f;
        float logW = std::log2((screenW > 1.0f ? screenW : 1.0f) / rw);
        float logH = std::log2((screenH > 1.0f ? screenH : 1.0f) / rh);
        float m = matchWidthOrHeight < 0.0f ? 0.0f : (matchWidthOrHeight > 1.0f ? 1.0f : matchWidthOrHeight);
        float t = logW * (1.0f - m) + logH * m;
        return std::pow(2.0f, t) * scaleFactor;
    }
};

} // namespace okay
