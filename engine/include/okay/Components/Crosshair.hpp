#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"

namespace okay {

/// A first-person / aim reticle drawn at screen center (Center anchor by default,
/// with an optional pixel offset via `position`). Renders four ticks around a
/// center gap and an optional center dot — the classic FPS crosshair. An optional
/// 1px dark outline keeps it readable over any background. `size` is the overall
/// reticle extent in pixels (its UI rect for selection/anchoring in the editor).
class Crosshair final : public Behaviour {
public:
    Vec2 position{0.0f, 0.0f};                        // offset from the anchor (px)
    Vec2 size{24.0f, 24.0f};                          // overall reticle extent (px)
    UIAnchor anchor = UIAnchor::Center;
    Color color = Color::FromBytes(255, 255, 255);

    float thickness = 2.0f;                           // line width (px)
    float gap = 6.0f;                                 // center gap (px)
    float length = 10.0f;                             // length of each of the 4 lines (px)

    bool  showLines = true;                           // the 4 ticks
    bool  lineUp = true, lineDown = true, lineLeft = true, lineRight = true; // per-tick (T-style: Up off)
    float spread = 0.0f;                              // extra gap added to every tick (aim spread, px)
    // grok/ui-hud — 0 leaves spread exactly as authored. Higher pulls it back toward 0 (px/sec).
    float spreadRecover = 0.0f;
    float spreadMax = 64.0f;
    void SetSpread(float px) { spread = px < 0.0f ? 0.0f : px; }
    void SetSpreadRecover(float pxPerSec) { spreadRecover = pxPerSec < 0.0f ? 0.0f : pxPerSec; }
    float Spread() const { return spread; }
    /// Push the ticks apart. They ease back while spreadRecover is above 0.
    void KickSpread(float px) {
        spread += px;
        if (spread < 0.0f) spread = 0.0f;
        if (spread > spreadMax) spread = spreadMax;
    }

    void Update(float dt) override {
        if (spreadRecover <= 0.0f || spread <= 0.0f) return;
        spread -= spreadRecover * dt;
        if (spread < 0.0f) spread = 0.0f;
    }

    bool  dot = false;
    float dotSize = 4.0f;
    Color dotColor = Color::FromBytes(255, 255, 255);

    bool  circle = false;                             // a ring around the centre
    float circleRadius = 12.0f;                       // ring radius (px)
    float circleThickness = 2.0f;                     // ring line width (px)
    Color circleColor = Color::FromBytes(255, 255, 255);

    bool  outline = true;                             // a 1px dark edge under the marks
    Color outlineColor = Color::FromBytes(0, 0, 0, 160);
};

} // namespace okay
