// The boundary between the C++/WinRT shell (compiled as C++20) and the engine (compiled as C++17): plain C++ only, no Windows, WinRT or engine
// header crosses it, so the two can use different language standards (the engine headers use u8string(), which changed in C++20; C++/WinRT wants
// C++20 coroutines). The shell fills ShellCallbacks and forwards CoreWindow events; engine_bridge.cpp (engine side) runs the game loop.
// EXPERIMENTAL, UNCOMPILED, UNVERIFIED. 2026-11-20: pointers, characters, pads and life-cycle events now go through XboxAppGlue.hpp (Linux-tested); this forwarding edit was never through a Windows compiler.
#pragma once
#include <cstdint>
#include <string>

namespace okay_xbox_bridge {

struct PadReadingPod { std::uint32_t buttons = 0; double leftTrigger = 0, rightTrigger = 0, leftX = 0, leftY = 0, rightX = 0, rightY = 0; };

/// What the engine side asks of the shell (Windows.Gaming.Input, the dispatcher, the audio device id). All called on the UI thread.
/// One CoreWindow pointer event, copied out of PointerEventArgs. kind: 0 mouse, 1 touch, 2 pen. action: 0 pressed, 1 moved, 2 released, 3 canceled, 4 wheel.
/// Position in DIPs; left / middle / right = the buttons held now; wheelDelta = MouseWheelDelta (120 per notch). The decisions are in XboxAppGlue.hpp.
struct PointerPod { long long id = 0; int kind = 0, action = 1; float dipX = 0, dipY = 0; bool left = false, middle = false, right = false; int wheelDelta = 0; };

struct ShellCallbacks {
    void* user = nullptr;
    void* (*coreWindow)(void* user) = nullptr;                          // the CoreWindow as an ::IUnknown*
    void (*pump)(void* user, bool wait) = nullptr;                      // CoreDispatcher.ProcessEvents (wait: ProcessOneAndAllPending)
    int (*padCount)(void* user) = nullptr;
    bool (*padRead)(void* user, int index, PadReadingPod* out) = nullptr;
    void (*padVibrate)(void* user, int index, double left, double right, double leftTrigger, double rightTrigger) = nullptr;
    int (*padKey)(void* user, int index, wchar_t* buffer, int capacity) = nullptr;   // writes Gamepad.NonRoamableId of pad `index`, returns its length (0 = none)
    int (*audioDeviceId)(void* user, wchar_t* buffer, int capacity) = nullptr;   // writes the default render device id, returns its length (0 = none)
};

struct Engine;   // opaque

/// Folders are UTF-8 with '/' or '\\' separators: the package install folder, ApplicationData.LocalFolder, TemporaryFolder.
Engine* Create(const ShellCallbacks& cb, const std::string& installDir, const std::string& localDir, const std::string& tempDir);
void Destroy(Engine* e);
/// One line into the log file (LocalState\\logs\\okayspace.log) and the in-memory log. The shell uses it for failures it catches.
void Log(Engine* e, const std::string& line);

// CoreWindow / CoreApplication events, forwarded by the shell.
void Resized(Engine* e, float dipsW, float dipsH, float dpi);
void Visibility(Engine* e, bool visible);
void Activated(Engine* e, bool active);
void Closed(Engine* e);
void BackRequested(Engine* e);
void Key(Engine* e, int virtualKey, bool down, bool repeat);
void Character(Engine* e, std::uint32_t utf16Unit);   // CharacterReceived: one UTF-16 code unit
void Pointer(Engine* e, const PointerPod& p);          // pressed / moved / released / canceled / wheel, mouse or touch or pen
/// Suspend: saves (runs the game's hook), trims the GPU. Synchronous; the shell completes its deferral right after. Resume re-checks the device.
void Suspending(Engine* e, std::uint64_t deadlineMs);
void Resuming(Engine* e);

/// The game loop. Returns true when the game ended normally (quit / back); false with `err` set when it could not start or failed.
bool Run(Engine* e, std::string& err);

} // namespace okay_xbox_bridge
