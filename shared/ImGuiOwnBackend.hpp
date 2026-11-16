#pragma once
// Our own Dear ImGui backend: platform side over okay::platform (events, clipboard, cursor, text input, timing) and
// renderer side over okay::platform::Painter. It replaces imgui_impl_sdl2 + imgui_impl_sdlrenderer2 for a program that
// has no SDL window. OFF by default (CMake -DOKAY_OWN_IMGUI=ON, then OKAY_IMGUI_BACKEND=own at run time);
// docs/own_imgui_backend.md. The plain logic is in ImGuiOwnInput.hpp (tested headless); this file adapts it to ImGui.
//
// Written from Dear ImGui's public API documentation (imgui.h). One instance per process (the clipboard callbacks of
// ImGui carry no user pointer of ours).
#include "ImGuiOwnInput.hpp"
#include "okay/Platform/Painter.hpp"

struct ImDrawData;
struct ImTextureData;

namespace okay::imguibackend {

class OwnBackend {
public:
    ~OwnBackend() { Shutdown(); }
    /// Needs a current ImGui context. `backend` and `window` may be null (headless: the display size comes from the
    /// painter, no clipboard, cursor or text input is touched). `painter` is required.
    bool Init(okay::platform::Backend* backend, okay::platform::Window* window, okay::platform::Painter* painter);
    void Shutdown();
    /// Feed every okay::platform event (only the ones this backend uses have an effect).
    void ProcessEvent(const okay::platform::Event& e);
    /// Call before ImGui::NewFrame(): display size, framebuffer scale, delta time, cursor, text input.
    void NewFrame();
    /// After ImGui::Render(): draws the frame through the painter. The caller clears the target before and presents after.
    void RenderDrawData(ImDrawData* dd);
    /// Content scale of the window (1.0 at 96 DPI) for the application to apply to fonts and style.
    float ContentScale() const;

private:
    void UpdateTexture(ImTextureData* t);
    void DestroyTexture(ImTextureData* t);
    okay::platform::Backend* m_backend = nullptr;
    okay::platform::Window* m_window = nullptr;
    okay::platform::Painter* m_painter = nullptr;
    InputTranslator m_input;
    FrameTimer m_timer;
    TextInputGate m_text;
    DisplayMetrics m_display;
    int m_cursor = 0;
    bool m_cursorSet = false, m_cursorShown = true;
    bool m_inited = false;
    std::vector<okay::ui2d::Vertex> m_verts;
    std::vector<int> m_idx;
    friend struct ClipboardHooks;
};

} // namespace okay::imguibackend
