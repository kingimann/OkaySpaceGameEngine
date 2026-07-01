#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"

namespace okay {

/// A non-interactive screen-space rectangle (menu backgrounds, HUD frames,
/// dimming overlays). Position/size are window pixels (origin top-left); alpha
/// is honored so you can lay translucent panels over the scene.
class UIPanel : public Behaviour {
public:
    Vec2 position{0.0f, 0.0f};
    Vec2 size{200.0f, 120.0f};
    Color color = Color::FromBytes(20, 24, 34, 200);
    UIAnchor anchor = UIAnchor::TopLeft;
    // Silhouette: a hard box, soft rounded corners (the modern flat look),
    // a full circle (avatars/badges), or a pill/capsule (toggle tracks, tags).
    UIShape shape = UIShape::Rounded;
    // Customization: rounded corners and an optional border.
    float cornerRadius = 4.0f;
    float borderWidth = 0.0f;                      // 0 = no border
    Color borderColor = Color::FromBytes(255, 255, 255, 60);
    // Optional gradient: `color` fades to `colorBottom`. `gradientDir` chooses the
    // direction — Vertical (top->bottom), Horizontal (left->right) or one of the two
    // diagonals. `gradientHorizontal` is kept for back-compat (old scenes) and is
    // mirrored from `gradientDir` on save; renderers read `gradientDir`.
    enum class GradientDir { Vertical, Horizontal, DiagonalDown, DiagonalUp };
    bool  useGradient = false;
    bool  gradientHorizontal = false;              // legacy mirror of gradientDir==Horizontal
    GradientDir gradientDir = GradientDir::Vertical;
    Color colorBottom = Color::FromBytes(10, 12, 18, 200);
    // Outer outline: a keyline/glow ring drawn OUTSIDE the panel edge (distinct from
    // `borderWidth`, which insets a border within the fill). Great for focus rings,
    // neon accents and separating a card from a busy background. 0 = off.
    float outlineWidth = 0.0f;
    Color outlineColor = Color::FromBytes(120, 180, 255, 180);
    // Inner top sheen: a subtle light band along the inside top edge — the classic
    // "glass"/glossy panel highlight. Alpha controls its strength.
    bool  topHighlight = false;
    Color highlightColor = Color::FromBytes(255, 255, 255, 40);
    // Optional drop shadow: a translucent copy drawn behind, offset by
    // `shadowOffset` pixels — lifts dialogs/cards off the scene.
    bool  shadow = false;
    Color shadowColor = Color::FromBytes(0, 0, 0, 120);
    Vec2  shadowOffset{6.0f, 6.0f};
    /// Shadow blur in pixels: 0 = a crisp shadow, higher = a soft penumbra (the
    /// premium card look). Stacked expanding copies fake the blur.
    float shadowSoftness = 0.0f;
};

} // namespace okay
