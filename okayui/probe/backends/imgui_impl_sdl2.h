#pragma once
// Port probe stand-in for the Dear ImGui SDL2 platform backend header: only the DECLARATIONS the editor and launcher call. Nothing defines
// them; each one is a call the OkayUI host (shared/OkayUIHost.hpp, okayuihost::platform) replaces. docs/own_ui_toolkit.md 6.18 lists them.
#include "imgui.h"
#include <SDL.h>
IMGUI_IMPL_API bool ImGui_ImplSDL2_InitForSDLRenderer(SDL_Window* window, SDL_Renderer* renderer);
IMGUI_IMPL_API bool ImGui_ImplSDL2_InitForOpenGL(SDL_Window* window, void* sdl_gl_context);
IMGUI_IMPL_API bool ImGui_ImplSDL2_ProcessEvent(const SDL_Event* event);
IMGUI_IMPL_API void ImGui_ImplSDL2_NewFrame();
IMGUI_IMPL_API void ImGui_ImplSDL2_Shutdown();
