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

namespace okay { class GameObject; }

namespace okay {
class Scene;
class TextRenderer;

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
/// `italic` shears every glyph quad about the baseline (a true slant, not a different font).
/// `boldOffset` > 0 draws each glyph three times across that many screen pixels, which thickens the
/// strokes smoothly (a faux bold: the font file has no bold weight).
void DrawTtfText(Painter& r, TtfFont* f, const std::string& text,
                 float ox, float oy, float px, RGBA col, float letterSp, float lineSp,
                 bool italic = false, float boldOffset = 0.0f);
/// The scene-wide default UI font DrawUIText/UITextW fall back to when no font is given.
/// RenderSceneUI sets it to the scene's UI font for its pass and clears it after, so
/// world text and overlays keep the 8x8 bitmap font unless a host sets it around them.
void SetUIDefaultFont(TtfFont* f);
TtfFont* UIDefaultFont();
/// Width of `text` in screen pixels at font-pixel size `px`, in the font DrawUIText will
/// use (the scene's UI font during RenderSceneUI, else the 8x8 bitmap font).
float UITextW(const std::string& text, float px);
/// Same measurement for an explicit font (nullptr = the scene UI font). Uses the
/// TrueType face baked at the on-screen pixel size when one is in use.
float UIMeasureText(TtfFont* font, const std::string& text, float px);
/// Where the caret is drawn on one line of text (no '\n') drawn by DrawUIText at x = 0, when it sits before
/// byte `caretByte` of `line`: the width of what is drawn to its left. Right-to-left text is drawn in visual
/// order, so this is not the width of the first `caretByte` bytes there (nullptr font = the scene UI font).
float UICaretX(TtfFont* font, const std::string& line, std::size_t caretByte, float px);
/// A clickable [url=...] run of rich text as it was last drawn: the rectangle (in the space the UI was drawn in, the same as
/// the mouse position when the canvas is not scaled) and the link's target.
struct RichLinkRect { float x = 0, y = 0, w = 0, h = 0; std::string target; };
/// The link rectangles the last frame drew for the text object `go` (empty for a text with no links). UIRichLinks reads this.
const std::vector<RichLinkRect>& RichLinksOf(const okay::GameObject* go);

/// An anti-aliased X (search clear, close). `size` is the box in logical pixels.
void DrawUICross(Painter& ren, float x, float y, float size, RGBA col);
/// Draw a string with the built-in 8x8 font as filled rects, top-left at (ox, oy), each
/// font pixel `px` screen pixels wide. With a TTF font (`ttf`, or the scene's UI font
/// during RenderSceneUI) the glyph atlas is used instead, in the same sizing space.
/// (Not named DrawText: windows.h #defines that to DrawTextA.)
void DrawUIText(Painter& r, const std::string& text, float ox, float oy,
                float px, RGBA col, float letterSp = 0.0f, float lineSp = 0.0f,
                bool italic = false, bool gradient = false, RGBA col2 = RGBA{0, 0, 0, 0},
                TtfFont* ttf = nullptr, bool bold = false);

/// Draw a world-anchored TextRenderer (screenSpace off) at screen point (`x`, `y`) with font pixel `px`: its
/// shadow and outline, and, when `richText` is on, its colour, bold, italic and size tags (okay/UI/RichText.hpp), with
/// the text's own bold and italic on every run, shadow and outline included. With rich text off it draws exactly as
/// world text always did (italic is not drawn, bold is a second copy one step to the right). `opacity` multiplies
/// every colour's alpha. The player's 2D world-text pass uses it.
void DrawWorldText(Painter& p, const TextRenderer& tr, float x, float y, float px, float opacity = 1.0f);

} // namespace ui2d
} // namespace okay
