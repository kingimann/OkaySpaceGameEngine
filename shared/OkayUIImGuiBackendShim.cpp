// The Dear ImGui backend entry points the launcher and the editor call, defined over OkayUI, for the executables built from the SAME sources against
// OkayUI's Dear ImGui compatibility layer (CMake option OKAY_EDITOR_ON_OKAYUI, macro OKAY_ON_OKAYUI; the launcher no longer needs them). The input side is
// shared/ImGuiPlatformFeed.hpp (okay::platform events into the IO events OkayUI merges at NewFrame), so the SDL event functions do nothing here;
// the draw side hands OkayUI's frame to the SDL renderer. docs/ui/own_ui_toolkit.md, "Running the real launcher and editor on OkayUI".
#include "okay/UI/OkayUI.hpp"
#include "imgui.h"                        // okayui/probe/imgui.h: the compatibility header
#include "backends/imgui_impl_sdl2.h"      // the declarations (okayui/probe/backends)
#include "backends/imgui_impl_sdlrenderer2.h"
#include <SDL.h>

IMGUI_IMPL_API bool ImGui_ImplSDL2_InitForSDLRenderer(SDL_Window*, SDL_Renderer*) { return true; }
IMGUI_IMPL_API bool ImGui_ImplSDL2_InitForOpenGL(SDL_Window*, void*) { return true; }
IMGUI_IMPL_API bool ImGui_ImplSDL2_ProcessEvent(const SDL_Event*) { return false; }
IMGUI_IMPL_API void ImGui_ImplSDL2_NewFrame() {}
IMGUI_IMPL_API void ImGui_ImplSDL2_Shutdown() {}
IMGUI_IMPL_API bool ImGui_ImplSDLRenderer2_Init(SDL_Renderer*) { return true; }
IMGUI_IMPL_API void ImGui_ImplSDLRenderer2_Shutdown() {}
IMGUI_IMPL_API void ImGui_ImplSDLRenderer2_NewFrame() {}
IMGUI_IMPL_API void ImGui_ImplSDLRenderer2_UpdateTexture(ImTextureData*) {}
IMGUI_IMPL_API void ImGui_ImplSDLRenderer2_RenderDrawData(ImDrawData*, SDL_Renderer* renderer) { OkayUI::EndFrame(renderer); }
