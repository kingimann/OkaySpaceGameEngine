#pragma once
// Small calls that code written for Dear ImGui uses a lot and that OkayUI did not have: the main viewport, colour conversion, theme
// colours as floats, content-region extents, scroll-to-cursor, window size constraints, and a few "accepted, does nothing" calls so
// ported code compiles unchanged (the same approach as the ignored StyleVar values in OkayUI.hpp). Built only on OkayUI's public
// API: no SDL, no Dear ImGui. Chosen from the ranked list in docs/imgui_port_report.md.
//
// What each call does here, and where it differs from Dear ImGui, is written at the declaration.
#include "okay/UI/OkayUI.hpp"

namespace OkayUI {

/// Four floats, 0..1 (ImVec4 for colours).
struct Vec4 { float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f; };

/// The area OkayUI draws into: position (0, 0) and the display size the host gave BeginFrame (Input::displayW/H; zeros when the host
/// gave none). OkayUI has one viewport, so there is no per-window or multi-monitor variant.
struct Viewport {
    Vec2 pos, size;
    Vec2 GetCenter() const { return Vec2{ pos.x + size.x * 0.5f, pos.y + size.y * 0.5f }; }
};
Viewport GetMainViewport();

/// Colour conversion between a packed 0xAABBGGRR value (Color32) and four floats 0..1. Out-of-range or non-finite floats are
/// clamped (NaN becomes 0), so a bad colour cannot produce a bad byte.
Vec4 ColorConvertU32ToFloat4(Color32 c);
Color32 ColorConvertFloat4ToU32(Vec4 v);
/// RGB <-> HSV, all 0..1 (hue 0..1 around the wheel). Non-finite input gives 0.
void ColorConvertRGBtoHSV(float r, float g, float b, float& h, float& s, float& v);
void ColorConvertHSVtoRGB(float h, float s, float v, float& r, float& g, float& b);

/// The active theme's colour for `which` as floats 0..1 (the colour PushStyleColor would replace). Includes any PushStyleColor in force
/// only as far as Style() does: it reads the theme itself, not the override stack.
Vec4 GetStyleColorVec4(Col which);

/// The far corner of the current window's content region, relative to the window (cursor position plus the space still available).
/// Dear ImGui's GetContentRegionMax is the window's full content size; here it is the remaining extent from the cursor, which is
/// what the calls in the editor use it for (fill to the edge).
Vec2 GetContentRegionMax();
Vec2 GetWindowContentRegionMax();

/// Scroll the current child window so the cursor row lands at `centerRatio` of its height (0 top, 0.5 middle, 1 bottom: the usual
/// "keep the newest line in view" call). The cursor is the top of the next item, so after the last line ratio 1 is the end. Outside a scrolling child it does nothing. The scroll applies when the child ends.
void SetScrollHereY(float centerRatio = 0.5f);
/// The visible height of the current scrolling child's content area (its height minus padding); 0 outside a child. Defined next to
/// SetScrollY in OkayUI.cpp (it needs the child stack); SetScrollHereY uses it.
float GetChildViewHeight();
/// Horizontal scrolling does not exist in OkayUI: GetScrollX is always 0 and SetScrollX does nothing.
float GetScrollX();
void SetScrollX(float x);

/// The next window gets a resize grip with `minSize` as its smallest size (ResizableNextWindow). `maxSize` is not enforced.
void SetNextWindowSizeConstraints(Vec2 minSize, Vec2 maxSize);

/// A stable id for a string: a 32-bit FNV-1a hash (never 0). Unlike Dear ImGui's it does not look at the ID stack.
unsigned GetID(const char* str);

/// Accepted and ignored, so ported code compiles: OkayUI has no keyboard-default-focus item, no overlap rule between items, and no
/// frozen table rows or columns.
void SetItemDefaultFocus();
void SetNextItemAllowOverlap();
void TableSetupScrollFreeze(int cols, int rows);

} // namespace OkayUI
