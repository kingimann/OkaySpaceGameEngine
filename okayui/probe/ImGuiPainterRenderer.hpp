#pragma once
// The UI's renderer for the programs built against OkayUI's compatibility layer (okayui/probe comes first on their include path): the
// entry points the launcher and the editor call (okay::imguirender::Init / Render / Shutdown); the frame is OkayUI's own, drawn through the
// window's okay::platform::Painter by the toolkit's frame drawer (okayui/soft/OkayUI_SoftPainter.hpp; the SDL painter takes its triangles too).
#include "imgui.h"
#include "okay/Platform/Painter.hpp"
#include "okay/UI/OkayUI.hpp"
#include "OkayUI_SoftPainter.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>

namespace okay::imguirender {

inline const char* ActiveLine() { return "UI drawing: OkayUI frame through okay::platform::Painter"; }
inline void Init(ImGuiIO& io) { io.BackendRendererName = "okayui_painter"; OkayUI::UseLauncherLook(true); }   // the launcher look: the TTF atlas, rounded frames
/// `dd` is unused: OkayUI's frame data is what ImGui::Render() (OkayUI::EndFrameData) just finished.
inline void Render(okay::platform::Painter& p, ImDrawData*) {
    static auto* drawers = new std::unordered_map<okay::platform::Painter*, std::unique_ptr<OkayUI::soft::FrameDrawer>>();
    auto& d = (*drawers)[&p];
    if (!d) d.reset(new OkayUI::soft::FrameDrawer(p));
    if (std::getenv("OKAYUI_TRACE")) { const OkayUI::DrawData dd = OkayUI::GetDrawData(); std::fprintf(stderr, "[okayui] %d vertices %d indices %d cmds %d dropped\n", dd.vertexCount, dd.indexCount, dd.cmdCount, dd.dropped); }
    d->Draw(OkayUI::GetDrawData());
}
namespace detail { inline void UpdateTexture(okay::platform::Painter&, ImTextureData*) {} }   // OkayUI owns its textures
inline void UpdateTexture(okay::platform::Painter&, ImTextureData*) {}
inline void Shutdown(okay::platform::Painter& p, ImGuiIO&) {
    static auto* none = new int(0); (void)none;
    (void)p;   // the drawer is kept per painter for the life of the program
}

} // namespace okay::imguirender
