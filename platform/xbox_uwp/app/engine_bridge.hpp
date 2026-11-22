// The boundary between the C++/WinRT shell (compiled as C++20) and the engine (compiled as C++17): plain C++ only, no Windows, WinRT or engine
// header crosses it, so the two can use different language standards (the engine headers use u8string(), which changed in C++20; C++/WinRT wants
// C++20 coroutines). The shell fills ShellCallbacks and forwards CoreWindow events; engine_bridge.cpp (engine side) runs the game loop.
// EXPERIMENTAL, UNCOMPILED, UNVERIFIED.
#pragma once
#include <cstdint>
#include <string>

namespace okay_xbox_bridge {

struct PadReadingPod { std::uint32_t buttons = 0; double leftTrigger = 0, rightTrigger = 0, leftX = 0, leftY = 0, rightX = 0, rightY = 0; };

/// What the engine side asks of the shell (Windows.Gaming.Input, the dispatcher, the audio device id). All called on the UI thread.
struct ShellCallbacks {
    void* user = nullptr;
    void* (*coreWindow)(void* user) = nullptr;                          // the CoreWindow as an ::IUnknown*
    void (*pump)(void* user, bool wait) = nullptr;                      // CoreDispatcher.ProcessEvents (wait: ProcessOneAndAllPending)
    int (*padCount)(void* user) = nullptr;
    bool (*padRead)(void* user, int index, PadReadingPod* out) = nullptr;
    void (*padVibrate)(void* user, int index, double left, double right, double leftTrigger, double rightTrigger) = nullptr;
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
void BackRequested(Engine* e);   // a back press for the game (closes a pause menu), never an exit
/// How the app is laid out: `fullScreen` = ApplicationViewBoundsMode.UseCoreWindow was accepted (the picture reaches the TV edges);
/// `deviceFamily` = AnalyticsInfo.VersionInfo.DeviceFamily. Sets the title-safe insets the UI keeps to (XboxTitleSafe.hpp).
void ScreenBounds(Engine* e, bool fullScreen, const std::string& deviceFamily);
void Key(Engine* e, int virtualKey, bool down, bool repeat);
void Text(Engine* e, const std::string& utf8);
void PointerMove(Engine* e, int x, int y);
void PointerButton(Engine* e, int button, bool down, int x, int y);
void Wheel(Engine* e, float notches);
/// Suspend: saves (runs the game's hook), trims the GPU. Synchronous; the shell completes its deferral right after. Resume re-checks the device.
void Suspending(Engine* e, std::uint64_t deadlineMs);
void Resuming(Engine* e);

/// The game loop. Returns true when the game ended normally (the game quit or the window closed); false with `err` set when it could not start or failed.
bool Run(Engine* e, std::string& err);

} // namespace okay_xbox_bridge
