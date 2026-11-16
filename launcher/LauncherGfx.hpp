#pragma once
// The launcher's drawing seam: its main window, the painter it draws on, image textures from RGBA pixels, the frame
// (clear, present) and Dear ImGui's two backends (input in, draw data out). Two implementations, picked at start-up
// by OKAY_LAUNCHER_RENDERER:
//
//   sdl  (the default)  SDL_CreateWindow + SDL_Renderer, ImGui's SDL2 platform and SDL_Renderer backends: what the
//                       launcher always did, call for call (LauncherGfxSdl.cpp).
//   own  (opt-in)       a window made through okay::platform, our OpenGL painter on its GL context (or the software
//                       painter when the window has no GL), our own ImGui renderer and platform glue
//                       (shared/ImGuiOwnRenderer.cpp). No SDL_Renderer, no ImGui SDL backends (LauncherGfx.cpp).
//
// OWN CODE, NOT READY: `own` is off by default until the owner approves it (docs/sdl_removal_plan.md, stage B).
// Header rules (MinGW): no <mutex> / <thread> here, no SDL types (they stay in LauncherGfxSdl.cpp).
#include "okay/Platform/Painter.hpp"
#include "okay/Platform/Platform.hpp"

#include <cstdint>
#include <memory>
#include <string>

struct ImDrawData;

namespace okay::launchergfx {

enum class RendererKind { Sdl, Own };

/// What OKAY_LAUNCHER_RENDERER asks for: "own" = Own; unset, empty, "sdl" or anything else = Sdl.
RendererKind ParseRenderer(const char* value);
const char* RendererName(RendererKind k);   // "sdl" / "own"

struct WindowSpec {
    std::string title = "OkaySpace";
    int x = okay::platform::kCentered, y = okay::platform::kCentered;   // screen position (kCentered = the middle)
    int width = 1280, height = 800;
    bool vsync = true;
};

class Gfx {
public:
    virtual ~Gfx() = default;
    virtual RendererKind Kind() const = 0;
    /// One line for the log, e.g. "own (opengl painter on a sdl2 window)".
    virtual std::string Describe() const = 0;

    /// The main window (created hidden) and the backend that made it.
    virtual okay::platform::Window& Window() = 0;
    virtual okay::platform::Backend& Backend() = 0;
    /// The painter the launcher draws on (the splash, ImGui, image textures).
    virtual okay::platform::Painter& Painter() = 0;
    /// SDL_Window* when the window is an SDL one (tray, taskbar flash, icon), else null.
    virtual void* SdlWindow() = 0;
    /// SDL_Renderer* in the sdl implementation (OkayUI's EndFrame still takes one), else null.
    virtual void* SdlRenderer() = 0;

    // ---- Dear ImGui -----------------------------------------------------------------------------
    /// Hook ImGui's backends up (the context and fonts exist; this context is current). Once.
    virtual bool InitImGui() = 0;
    virtual void ShutdownImGui() = 0;
    /// Before ImGui::NewFrame.
    virtual void NewFrame() = 0;
    /// sdl: give one SDL_Event (an SDL_Event*) to ImGui. own: does nothing.
    virtual void ProcessSdlEvent(const void* sdlEvent) { (void)sdlEvent; }
    /// own: the next okay::platform event, already given to ImGui. sdl: always false (main.cpp reads SDL's queue).
    virtual bool PollEvent(okay::platform::Event& e) { (void)e; return false; }
    /// own: give ImGui one event that another Gfx's PollEvent took (the chat window's events arrive through the launcher's
    /// poll: one queue for every window). Call with this Gfx's ImGui context current. sdl: does nothing (ProcessSdlEvent).
    virtual bool FeedEvent(const okay::platform::Event& e) { (void)e; return false; }
    virtual void RenderImGui(ImDrawData* dd) = 0;

    // ---- the frame and textures (the same on both: the painter does them) -------------------------
    void BeginFrame(std::uint8_t r, std::uint8_t g, std::uint8_t b) { Painter().SetDrawColor(r, g, b, 255); Painter().Clear(); }
    void Present() { Painter().Present(); }
    /// An image texture from tightly packed RGBA8 pixels (R, G, B, A bytes, top row first): blended, nearest-sampled,
    /// as SDL_CreateTextureFromSurface made them. Null on failure.
    okay::ui2d::Tex CreateImageTexture(const std::uint8_t* rgba, int w, int h) {
        if (!rgba || w <= 0 || h <= 0) return nullptr;
        okay::platform::TextureDesc d;
        d.width = w; d.height = h;
        d.blend = okay::platform::BlendMode::Blend;
        d.scale = okay::platform::ScaleMode::Nearest;
        return Painter().CreateTexture(d, rgba);
    }
};

/// The sdl implementation (LauncherGfxSdl.cpp). Null (and `err`) when SDL cannot make the window or the renderer.
/// Call it again for a second window (the chat's): each Gfx has its own window, renderer and ImGui backends (hook them up
/// with that window's ImGui context current).
std::unique_ptr<Gfx> CreateSdlGfx(const WindowSpec& spec, std::string* err = nullptr);
/// The own implementation. Null (and `err`) when okay::platform has no backend, no window or no painter for it.
/// A second call makes a second window with its own GL context and painter (textures are not shared between them: each
/// painter uploads its own), from the same okay::platform backend, so its events come through the first one's PollEvent.
std::unique_ptr<Gfx> CreateOwnGfx(const WindowSpec& spec, std::string* err = nullptr);

/// OKAY_LAUNCHER_FRAME_STATS=N: the median and 95th percentile of N frame times, logged once. Fed by main.cpp.
class FrameStats {
public:
    explicit FrameStats(int frames) : m_target(frames) {}
    bool Active() const { return m_target > 0 && !m_done; }
    /// One frame's work in milliseconds. Returns true once, when the N-th frame arrives (then Report() is ready).
    bool Add(double ms);
    std::string Report(const char* what) const;
private:
    int m_target = 0;
    bool m_done = false;
    std::unique_ptr<double[]> m_ms;
    int m_n = 0;
};

} // namespace okay::launchergfx
