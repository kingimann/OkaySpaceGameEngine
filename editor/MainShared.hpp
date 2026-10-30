#pragma once
// Functions and state still defined in main.cpp that the split-out panels use.
// This list shrinks as main.cpp is split further; each entry moves to the
// header of the module that ends up owning it.
#include "imgui.h"
#include <string>

namespace okay { class GameObject; namespace editor { class EditorState; } }
struct SDL_Texture;

namespace oked {

// Window visibility and play-mode flags (main.cpp).
extern bool g_showConsole;
extern bool g_paused;              // the simulation is paused while staying in Play
extern bool g_clearConsoleOnPlay;  // wipe the Console each time Play starts
extern float g_editorFov;          // the Scene view's vertical field of view (degrees)

// The timeline widgets the Animation window's editors share (main.cpp): the
// Timeline window draws its transport, ruler, playhead and keys with the same
// ones, so the two tools read alike.
// A square toolbar button with a drawn glyph. `kind`: 0 record, 1 play, 2 pause,
// 3 go to start, 4 previous key, 5 next key, 6 add key, 7 preview, 8 event flag,
// 9 curves, 10 more, 11 presets, 12 go to end, 13 loop, 14 onion skin, 15 fit,
// 16 key rows. An active button uses the accent, or `activeCol` when given.
bool AnimGlyphButton(const char* id, int kind, const char* tip, bool active, ImU32 activeCol = 0);
// The transport: go to start, previous key, play / pause, next key, go to end.
// Returns 0..4 for the button pressed (in that order), -1 for none.
int AnimTransport(bool playing);
// The visible stretch of a timeline: `start` seconds at the left edge and `len`
// seconds across (0 = not set yet: fit the clip).
struct AnimTimeView { float start = 0.0f, len = 0.0f; };
void AnimTimeViewFit(AnimTimeView& v, float clipLen);
// Wheel zooms about the cursor (`mouseT` is the time under it), middle-drag pans.
void AnimTimeViewInput(AnimTimeView& v, float pxPerSec, float mouseT);
// A time ruler across the rect from `o`, `W` wide and `h` tall, with grid lines
// down to `gridBottom` and the stretch past `clipLen` shaded; `pad` is the margin
// the times are inset by.
void AnimDrawRuler(ImDrawList* dl, ImVec2 o, float W, float h, float gridBottom,
                   const AnimTimeView& v, float pad, float clipLen, int fps);
// The playhead: a line from `top` to `bottom` with a flag at the top; red while recording.
void AnimDrawPlayhead(ImDrawList* dl, float x, float top, float bottom, bool recording);
// A key diamond of radius `r` at `c`: filled in `col` or hollow; white while hovered
// or selected, outlined when it sits under the playhead.
void AnimDrawDiamond(ImDrawList* dl, ImVec2 c, float r, ImU32 col, bool filled, bool hot, bool selected, bool atHead);

// Script Editor: open a script file in a tab, scrolled to `line` (1-based).
void OpenScriptFileInEditorAt(const std::string& path, int line);

// "Add animations from file...": pick clip files (FBX, glTF, DAE, BVH) and add
// every clip to the Model Animator on `modelRoot`.
void AddAnimationsFromFilesUI(okay::editor::EditorState& ed, okay::GameObject* modelRoot);

// Scene-view snapping (Snap toggle, or Ctrl held) and gizmo axes; the spline point
// gizmo follows them like the move gizmo does.
extern bool  g_snap;
extern float g_snapSize;
extern bool  g_gizmoLocal;
// An image file (or "proc:" texture) as a cached SDL texture, null when it can't load.
SDL_Texture* GetThumb(const std::string& path);

} // namespace oked
