#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include <string>

namespace okay {

/// A screen-space image (logos, icons, HUD frames, title art). Draws `texture`
/// stretched into the pixel rect at `position`/`size` (origin top-left), tinted
/// by `color`. With no texture it falls back to a flat colored rectangle, so it
/// doubles as a solid/translucent panel. Build Game bundles the image file.
class UIImage : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{128.0f, 128.0f};
    std::string texture;                 // image path (PNG/JPG/BMP); empty = colored rect
    Color color = Color::White;          // tint (or fill color when no texture)
    UIAnchor anchor = UIAnchor::TopLeft;
    /// Nine-slice scaling: keep a `border`-pixel frame from the source texture
    /// undistorted while the edges and center stretch to fill `size`. Ideal for
    /// resizable panels, frames, and buttons from one small bordered image.
    bool  nineSlice = false;
    float border = 16.0f;                // source-pixel inset for the 9 regions
    /// Radial/linear fill (cooldowns, health bars, loading). When `fillMode` is
    /// not None, only `fillAmount` (0..1) of the rect is drawn, revealed along
    /// the chosen axis/direction.
    enum class FillMode { None, Left, Right, Up, Down };
    FillMode fillMode = FillMode::None;
    float fillAmount = 1.0f;
    /// Rounded corners for the colored-rect fallback (pixels).
    float cornerRadius = 0.0f;

    /// The visible sub-rectangle after applying the fill (origin + size in local
    /// pixels relative to the widget's top-left). Returns the full rect when the
    /// fill is None or full.
    void FilledRect(float w, float h, float& ox, float& oy, float& fw, float& fh) const {
        ox = 0; oy = 0; fw = w; fh = h;
        float f = fillAmount < 0 ? 0 : (fillAmount > 1 ? 1 : fillAmount);
        switch (fillMode) {
            case FillMode::Right: fw = w * f; break;
            case FillMode::Left:  fw = w * f; ox = w - fw; break;
            case FillMode::Down:  fh = h * f; break;
            case FillMode::Up:    fh = h * f; oy = h - fh; break;
            default: break;
        }
    }
};

} // namespace okay
