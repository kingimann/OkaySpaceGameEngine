#pragma once
// Port probe stand-in for the Dear ImGui SDL_Renderer backend header (declarations only; see imgui_impl_sdl2.h).
#include "imgui.h"
#include <SDL.h>
IMGUI_IMPL_API bool ImGui_ImplSDLRenderer2_Init(SDL_Renderer* renderer);
IMGUI_IMPL_API void ImGui_ImplSDLRenderer2_Shutdown();
IMGUI_IMPL_API void ImGui_ImplSDLRenderer2_NewFrame();
IMGUI_IMPL_API void ImGui_ImplSDLRenderer2_RenderDrawData(ImDrawData* draw_data, SDL_Renderer* renderer);
IMGUI_IMPL_API void ImGui_ImplSDLRenderer2_UpdateTexture(ImTextureData* tex);
