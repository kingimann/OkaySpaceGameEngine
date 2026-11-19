#pragma once
// Stand-in for shared/ImGuiPainterRenderer.hpp in the executables built against OkayUI (okayui/probe comes first on their include path): the same
// entry points the launcher and the editor call (okay::imguirender::Init / Render / Shutdown), but the frame is OkayUI's own, drawn through the
// window's okay::platform::Painter by the toolkit's frame drawer (okayui/soft/OkayUI_SoftPainter.hpp; the SDL painter takes its triangles too).
#include "imgui.h"
#include "okay/Platform/Painter.hpp"
#include "okay/UI/OkayUI.hpp"
#include "OkayUI_SoftPainter.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace okay::imguirender {

inline bool OwnRequested(const char*) { return true; }
inline const char* ActiveLine(bool) { return "UI drawing: OkayUI frame through okay::platform::Painter (the OkayUI build; Dear ImGui is not in this program)"; }
inline void Init(ImGuiIO& io) { io.BackendRendererName = "okayui_painter"; }
/// `dd` is unused: OkayUI's frame data is what ImGui::Render() (OkayUI::EndFrameData) just finished.
inline void Render(okay::platform::Painter& p, ImDrawData*) {
    static auto* drawers = new std::unordered_map<okay::platform::Painter*, std::unique_ptr<OkayUI::soft::FrameDrawer>>();
    auto& d = (*drawers)[&p];
    if (!d) d.reset(new OkayUI::soft::FrameDrawer(p));
    d->Draw(OkayUI::GetDrawData());
}
namespace detail { inline void UpdateTexture(okay::platform::Painter&, ImTextureData*) {} }   // OkayUI owns its textures
inline void UpdateTexture(okay::platform::Painter&, ImTextureData*) {}
inline void Shutdown(okay::platform::Painter& p, ImGuiIO&) {
    static auto* none = new int(0); (void)none;
    (void)p;   // the drawer is kept per painter for the life of the program
}

} // namespace okay::imguirender
