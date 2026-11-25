// Private to engine/src/UI/UIRenderer.cpp and UIScenePass.cpp: what the scene pass (UIScenePass.cpp) shares with the
// drawing helpers and state that stay in UIRenderer.cpp. Not installed, not for other files.
#pragma once
#include "okay/UI/UIRenderer.hpp"
#include "okay/Core/AccessibilitySettings.hpp"
#include "okay/UI/UIFontPolicy.hpp"
#include "okay/UI/RichText.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {
namespace ui2d {
namespace ui_pass {
void RenderSceneUIImpl(Scene& scene, Painter& target, int w, int h, const UIRenderOptions& opt);

// State of the UI pass in progress (defined in UIRenderer.cpp).
extern float g_uiDeviceScale;
extern bool g_uiSmoothEdges;
extern UIQuality g_uiQuality;
extern TtfFont* g_uiDefaultFont;

// The player's text size (Accessibility), for the widgets whose text can grow without a fixed slot.
inline float UITextScale() { return Accessibility::Get().textScale; }

std::unordered_map<const okay::GameObject*, std::vector<RichLinkRect>>& RichLinkTable();
void DrawRichRuns(Painter& r, const std::string& disp, const std::vector<okay::RichStyle>& styles,
                  float ox, float oy, float px, RGBA base, float letterSp, float lineSp, bool italic,
                  bool bold, float boldOff, TtfFont* ttf,
                  const std::vector<std::string>* links = nullptr, const std::vector<std::string>* icons = nullptr, std::vector<RichLinkRect>* linkRects = nullptr);
} // namespace ui_pass
} // namespace ui2d
} // namespace okay
