#pragma once
// Multiplayer (Multiplayer.cpp). Moved out of launcher/main.cpp unchanged (docs/engine/modularization_plan.md,
// Part C section 16, batch L8).
#include "LauncherShared.hpp"

namespace okl {

// What main.cpp uses from Multiplayer.cpp.
extern SDL_Window* g_launcherWindow;
void PumpMultiplayer();
void PumpParty();
void DrawMpDialogs();
void DrawMultiplayerPage(bool signedIn);
} // namespace okl
