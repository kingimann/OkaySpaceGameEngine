#pragma once
// The Game view's device emulator (feature map 12.9.4), the Dear ImGui part: the toolbar controls, the device frame drawn
// by our own vector code, the status bar, the on-screen keyboard, touch points, the rotate animation, screenshots and
// what the frame loop feeds the game (touches, keys, the motion sensor, the Screen API, the conditions). The maths and
// the device table are in DeviceEmulatorModel.hpp (tested without a UI library). editor/main.cpp only calls these.
#include "imgui.h"
#include "DeviceEmulatorModel.hpp"
#include <string>

namespace oked {

// ---- the device and its settings (kept per project: <project>/ProjectSettings/device_emulator.cfg) ----
bool DevEmuActive();                                   // a device is picked
const okay::devemu::DeviceSpec* DevEmuDevice();        // the picked device (null: off)
okay::devemu::Orientation DevEmuOrientation();
void DevEmuScreenPixels(int& w, int& h);              // the picked device's screen as it is turned (0 x 0: off)
/// The old editor-wide keys of okay_settings.txt (gamedevice, gamedevice2, gamedeviceland, gamesafearea): true when `key`
/// was one. A project's own file, once loaded, wins over them.
bool DevEmuLoadLegacySetting(const std::string& key, int value);
/// The lines SaveSettings writes for those keys (so an older editor still finds the device).
std::string DevEmuLegacySettingLines();
/// Read the project's settings file (none yet: keep what okay_settings.txt said). "" = no project.
void DevEmuLoadProject(const std::string& projectDir);
void DevEmuSave();

// ---- the Game view ----
/// The Device dropdown, then the device's own controls (rotate, zoom, Back, the "..." menu with everything else).
/// Returns true while a device is picked (the Screen dropdown is not shown then).
bool DevEmuToolbar(bool playing);
/// Where the device screen goes in a region of the view: its top-left, its size, and the HUD preview scale (view units
/// per device pixel). `fitOnly`: always Fit (the UI editor). With no device: false, nothing set.
bool DevEmuPlace(ImVec2 origin, ImVec2 avail, bool fitOnly, ImVec2& pos, ImVec2& size, float& resScale);
/// How many more devices are shown beside it (0..3), and the region of each: cell 0 is the main device.
int DevEmuExtraCount();
ImVec2 DevEmuCell(ImVec2 origin, ImVec2 avail, int cell, ImVec2& cellSize);
/// Draw an extra device: Begin sets the engine's screen state (UICanvas, safe area, HUD scale) for it and gives its
/// screen; the caller draws the scene there; End puts the main device's back.
bool DevEmuBeginExtra(int i, ImVec2 origin, ImVec2 avail, ImVec2& pos, ImVec2& size, float& resScale);
void DevEmuEndExtra(ImDrawList* dl);
/// The frame behind the screen (before the game) and what lies on it (after the game): corners, cut-out, status bar,
/// home bar or navigation buttons, the safe-area tint, the keyboard, touch points. The overlay also takes the clicks on
/// the keyboard and the navigation buttons.
void DevEmuDrawBody(ImDrawList* dl, ImVec2 sp, ImVec2 se);
void DevEmuDrawOverlay(ImDrawList* dl, ImVec2 sp, ImVec2 se, bool playing, bool hovered);
/// Turn everything drawn between Begin and End by the rotate animation (no-op when it is not running).
void DevEmuRotateBegin(ImDrawList* dl);
void DevEmuRotateEnd(ImDrawList* dl, ImVec2 regionPos, ImVec2 regionSize);
/// Where the Game view's labels go so the notch, status bar and rounded corner do not hide them.
void DevEmuLabelOffsets(float& drop, float& in);
/// The pixel size the game's 3D picture is rendered at for the device being drawn (its own resolution). False: no device.
bool DevEmuRenderSize(float canvasW, float canvasH, float framebufferScale, int& w, int& h);
/// Which of the editor's 3D view textures the device being drawn uses (1 = the Game view's; extra devices 4, 5, 6).
int DevEmuRenderSlot();

// ---- the frame loop ----
/// The mouse as a finger: turn this frame's pointer (game pixels, the buttons pressed on the Game view) into touch
/// events, and rewrite the mouse the game sees as a phone's (no hover, the left button while a finger is down).
/// False (nothing changed) when no device or touch input is off.
bool DevEmuFeedPointer(float& x, float& y, unsigned& mask, bool twoFinger, bool pan);
/// What the on-screen keyboard and the Back button typed since the last call.
std::string DevEmuTakeTyped();
int DevEmuTakeBackspaces();
bool DevEmuTakeEnter();
bool DevEmuTakeBack();
/// Once a frame, after the Game view: the Screen API (size, dpi, orientation, safe area, keyboard, theme), the motion
/// sensor, auto-rotate, the network condition, "this is a touch screen"; and their desktop values back when it is off.
void DevEmuPublish(bool playing);
/// The frame cap to sleep to: the device's slow-device cap while playing on it, when lower than the editor's.
int DevEmuFpsCap(int editorCap, bool playing);
/// A screenshot asked for: its file and the device screen's rectangle in the main window's pixels. False: none due.
bool DevEmuTakeShot(std::string& path, int& x, int& y, int& w, int& h);

} // namespace oked
