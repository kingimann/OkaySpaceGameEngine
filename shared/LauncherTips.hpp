#pragma once
// The launcher's Tips page: its categories and tips as plain data, shared by the Dear ImGui page and the OkayUI one so both show
// the same words. Std only.
#include <cstddef>

namespace launchertips {

struct Tip { int cat; const char* title; const char* body; };   // cat indexes kCats (0 is "All": no tip has it)

inline const char* const kCats[] = {"All", "Editor", "Scripting", "Building", "Launcher"};
inline constexpr int kCatCount = (int)(sizeof(kCats) / sizeof(kCats[0]));

inline const Tip kTips[] = {
        {1, "Test instantly", "Press Play in the editor toolbar to try the scene right away."},
        {1, "Save everything", "Ctrl+S saves the scene, Ctrl+Shift+S saves it under a new name and Ctrl+Alt+S saves the whole project."},
        {1, "Test on phone screens", "In the Game view, pick a Device to preview phone and tablet sizes. Flip to landscape and show the safe area around notches."},
        {1, "Drag assets onto objects", "Drag textures, models and scripts from the Project panel onto objects in the Scene or Hierarchy."},
        {1, "Add UI", "GameObject > UI adds buttons, sliders, text and radial loaders."},
        {1, "Camera feel", "The Camera inspector has Zoom, Dutch Angle, Lens Shift and Camera Shake. Use Test Shake to preview it."},
        {2, "Autocomplete with examples", "Ctrl+Space opens suggestions. The panel beside the list shows what the selected item does, with an example."},
        {2, "Jump between problems", "Errors and warnings appear while you type. Press F2 to jump to the next one."},
        {2, "More room to code", "Alt+1 folds the scripts sidebar in the Script Editor."},
        {2, "One-line timers", "if (timer(\"spawn\", 2)) runs every 2 seconds, cooldown(\"shoot\", 0.3) limits how often something happens and once(\"intro\") runs a single time."},
        {2, "Shake the camera from code", "camera_shake(0.6) for hits and explosions; camera_zoom_to(2, 0.5) zooms in over half a second."},
        {2, "Movement in one line", "on_key_move(5) moves with WASD or the arrow keys; platformer(6, 8) is a whole 2D side-scroller controller."},
        {3, "Choose the scenes you ship", "File > Build Settings > Scenes In Build: tick the scenes to include. The top one loads first."},
        {3, "Fullscreen toggle", "Turn on the fullscreen toggle in Build Settings so players can press Alt+Enter or F11."},
        {3, "Share on GitHub", "Right-click a project on the Create page to publish it to GitHub, then use Commit & Push for later changes."},
        {3, "Play web games", "Drop an .html game (or a folder with index.html) into your games folder and it shows up under Play."},
        {4, "Jump between pages", "When you're not typing, the number keys switch pages (1 Create, 2 Play, 3 Community). Ctrl+F searches the current page."},
        {4, "Chat your way", "Chat can open in its own window or dock inside the launcher. Use the dock / pop-out button in the chat window."},
        {4, "Collapse the sidebar", "The panel button next to the logo shrinks the sidebar to icons."},
        {4, "Check for updates", "Click the version number under the logo to check for an update right now."},
        {4, "Notifications", "The bell keeps friend requests, mentions and release news. Read and cleared items stay that way after a restart."},
};
inline constexpr int kTipCount = (int)(sizeof(kTips) / sizeof(kTips[0]));

} // namespace launchertips
