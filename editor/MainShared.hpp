#pragma once
// Functions and state still defined in main.cpp that the split-out panels use.
// This list shrinks as main.cpp is split further; each entry moves to the
// header of the module that ends up owning it.
#include "imgui.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace okay { class GameObject; namespace editor { class EditorState; } }
#ifdef OKAY_HAVE_OKAYUI
namespace okayuihost { class Region; }
#endif
struct SDL_Texture;

namespace oked {

// Window visibility and play-mode flags (main.cpp).
extern bool g_showConsole;
extern bool g_paused;              // the simulation is paused while staying in Play
extern bool g_clearConsoleOnPlay;  // wipe the Console each time Play starts
extern bool g_consoleCollapse;     // Console 'Collapse' toggle: merge identical messages into one row with a count (okay_settings.txt)
void SaveSettings();               // writes okay_settings.txt
extern float g_editorFov;          // the Scene view's vertical field of view (degrees)
extern float g_uiScale;            // the editor's UI scale (1.0 = 100%)
// The opt-in OkayUI backend (docs/ui/own_ui_toolkit.md, section 5). True when the setting or OKAY_UI_BACKEND picked OkayUI and this
// build has it: the panels with an OkayUI port (the Console, today) are drawn by OkayUI into a rectangle they reserve in ImGui.
bool UseOkayUIBackend();
// The Inspector drawn by OkayUI (preview, off by default): the backend above plus OKAY_DEV or OKAY_UI_INSPECTOR (Settings > Experimental).
bool UseOkayUIInspector();
// The Script Editor's code is drawn by our own OkayUI::CodeEdit: always true in a build with OkayUI (the vendored
// ImGuiColorTextEdit it replaced is deleted). Works with the Dear ImGui backend too.
bool UseOkayUICodeEdit();
// OkayUI gets the mouse over its rectangles and the keys while one of its fields has the keyboard: the OkayUI backend, or the code editor above.
bool OkayUIInputOn();
#ifdef OKAY_HAVE_OKAYUI
okayuihost::Region& OkayUIRegion();   // where the hosted panel is, for the mouse (main.cpp)
#endif

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
// Attach a script file (.okay, .lua, .js) to an object as a new Script component, one undo step (main.cpp).
bool AttachScriptAsset(okay::editor::EditorState& ed, okay::GameObject* go, const std::string& path);

// "Add animations from file...": pick clip files (FBX, glTF, DAE, BVH) and add
// every clip to the Model Animator on `modelRoot`.
// onDone runs after clips were added (the picker may answer on a later frame, so work that depends on the result goes there).
void AddAnimationsFromFilesUI(okay::editor::EditorState& ed, okay::GameObject* modelRoot, std::function<void()> onDone = {});

// Scene-view snapping (Snap toggle, or Ctrl held) and gizmo axes; the spline point
// gizmo follows them like the move gizmo does.
extern bool  g_snap;
extern float g_snapSize;
extern bool  g_gizmoLocal;
// An image file (or "proc:" texture) as a cached SDL texture, null when it can't load.
SDL_Texture* GetThumb(const std::string& path);

// Run a shell command without flashing a console window (main.cpp); returns the exit code like std::system.
int SysNoWindow(const std::string& cmd);
// The Scripting Reference window's visibility (Help > Scripting Reference, the Script Editor's Docs), the Script
// Editor's monospace face, and the accent-ticked section heading every panel uses (main.cpp).
extern bool g_showScriptDocs;
extern ImFont* g_codeFont;
void SectionHeader(const char* label);
// Panel and window visibility the split-out windows read or set (main.cpp).
extern bool g_showProject;
extern bool g_showAssetManager;
extern bool g_showPluginManager;
// The Project panel scrolls to, selects and flashes this asset path next frame, then clears it (main.cpp).
extern std::string g_pingAsset;
// The folder the editor executable lives in (main.cpp).
std::string OkayExeDir();
// A model file extension the importer reads (lower case, with the dot) (main.cpp).
bool IsModelExt(const std::string& extLower);
// External-IDE helpers (main.cpp, namespace extide): write and read a text file, show a path in the file manager.
namespace extide {
bool WriteFile(const std::string& path, const std::string& text);
std::string ReadFile(const std::string& path);
void RevealInFiles(const std::string& path);
} // namespace extide
// The open project's settings (main.cpp; the struct is in ProjectState.hpp).
struct ProjectSettings;
extern ProjectSettings g_project;
#if defined(OKAY_XBOX_UWP_EXPORT)
// The last Xbox export's result (main.cpp), set by Build Game's Xbox export.
extern bool g_xboxExportOk;
extern std::vector<std::string> g_xboxExportWarnings;
#endif

} // namespace oked
