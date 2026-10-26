#pragma once
// The in-game UI renderer: the single sorted UI pass that draws every screen-space
// widget of a scene (panels, images, controls, text, minimap, crosshair, tooltips...)
// onto a ui2d::Painter. It used to live in the player's main loop; the player now
// calls RenderSceneUI with its SDL painter, and the drawing helpers below are shared
// with the player's other overlays (inventory, crafting, loading screen).
#include "okay/UI/UIPainter.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <vector>

namespace okay {
class Scene;

namespace ui2d {

/// How RenderSceneUI treats a scene. The defaults are the running game's.
struct UIRenderOptions {
    /// Live UI: widgets react to input (the minimap's click-to-place waypoint) and
    /// show their animated state. False for an editor's edit-mode preview: no input
    /// is acted on and animated widgets show their resting state (a toggle that is
    /// on draws on, without waiting for its slide-in animation to run).
    bool interactive = true;
    /// Project WorldUI labels through the caller's UIWorld() context instead of the
    /// scene's main camera, so an editor view with its own camera lines them up.
    bool worldLabelsViaUIWorld = false;
};

/// Draw every screen-space UI widget of `scene` in one pass, sorted by Canvas sort
/// order, uiDrawOrder / type layer and hierarchy (see SortUIDrawItems), on an output
/// of `width` x `height` pixels. World-space canvases project through whatever
/// UIWorld() context the caller installed. Leaves the painter unclipped at scale 1.
void RenderSceneUI(Scene& scene, Painter& p, int width, int height, const UIRenderOptions& opt = {});

// ---- Drawing helpers --------------------------------------------------------------

/// Fill a UI shape (rectangle / rounded / circle / pill...) into rect `r`, scanline by
/// scanline so any silhouette uses one code path. Supports a linear gradient
/// (top->bottom, or left->right when `horizontal`); pass equal colors for a flat fill.
/// `op` is the canvas master opacity. `diag`: 0 = axis-aligned (obey `horizontal`);
/// 1 = diagonal top-left->bottom-right; 2 = diagonal bottom-left->top-right.
void FillUIShape(Painter& ren, const Rect& r, UIShape shape, float radius,
                 const Color& top, const Color& bottom, bool gradient, bool horizontal,
                 float op, int diag = 0, int cornerMask = UICornerAll);
/// A border ring: the outer shape minus the shape inset by `width`.
void FillUIRing(Painter& ren, const Rect& r, UIShape shape, float radius,
                const Color& c, int width, float op, int cornerMask = UICornerAll);
/// Map a panel's gradient direction to FillUIShape's (horizontal, diag) params.
void GradientParams(UIPanel::GradientDir d, bool& horizontal, int& diag);
/// A drop shadow for a UI shape (softness 0 = crisp; > 0 = a stacked soft penumbra).
void FillUIShadow(Painter& ren, const Rect& r, UIShape shape, float radius,
                  const Color& color, float softness, float op, int cornerMask = UICornerAll);

// Small raster helpers (filled circle / ring / triangle / thick line / convex polygon).
void MMFillCircle(Painter& r, int cx, int cy, int rad, RGBA col);
void MMDrawRing(Painter& r, int cx, int cy, int rad, int width, RGBA col);
void MMFillTriangle(Painter& r, Point a, Point b, Point c, RGBA col);
void MMThickLine(Painter& r, float x0, float y0, float x1, float y1, float width, RGBA col);
void MMFillPoly(Painter& r, const std::vector<Point>& pts, RGBA col);

/// Draw a string with a TTF font (a textured glyph quad per character), tinted by col.
void DrawTtfText(Painter& r, TtfFont* f, const std::string& text,
                 float ox, float oy, float px, RGBA col, float letterSp, float lineSp);
/// The scene-wide default UI font DrawUIText/UITextW fall back to when no font is given.
/// RenderSceneUI sets it to the scene's UI font for its pass and clears it after, so
/// world text and overlays keep the 8x8 bitmap font unless a host sets it around them.
void SetUIDefaultFont(TtfFont* f);
TtfFont* UIDefaultFont();
/// Width of `text` in screen pixels at font-pixel size `px`, in the font DrawUIText will
/// use (the scene's UI font during RenderSceneUI, else the 8x8 bitmap font).
float UITextW(const std::string& text, float px);
/// Draw a string with the built-in 8x8 font as filled rects, top-left at (ox, oy), each
/// font pixel `px` screen pixels wide. With a TTF font (`ttf`, or the scene's UI font
/// during RenderSceneUI) the glyph atlas is used instead, in the same sizing space.
/// (Not named DrawText: windows.h #defines that to DrawTextA.)
void DrawUIText(Painter& r, const std::string& text, float ox, float oy,
                float px, RGBA col, float letterSp = 0.0f, float lineSp = 0.0f,
                bool italic = false, bool gradient = false, RGBA col2 = RGBA{0, 0, 0, 0},
                TtfFont* ttf = nullptr);

} // namespace ui2d
} // namespace okay
