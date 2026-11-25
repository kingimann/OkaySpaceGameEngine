#pragma once
// The Win32 side of the launcher's tray icon (launcher/TrayLogic.hpp TrayShell): Shell_NotifyIconW, the tray menu,
// hiding and showing the window. Built on Windows only (launcher/TrayWin32.cpp); nothing here includes <windows.h>.
#include "TrayLogic.hpp"
#include <cstdint>
#include <memory>

namespace okaytray {

/// Called on the main thread with every message for the tray's own window that the launcher may care about: the
/// icon's callback message, "TaskbarCreated", and every other registered (0xC000 and up) broadcast message, such as
/// the second-launch "come forward" message.
using MessageSink = void (*)(unsigned msg, uint64_t wParam, int64_t lParam);

/// A shell for the launcher's main window (its HWND). The icon belongs to a hidden window of our own with its own
/// window procedure, so its clicks reach us whatever the platform backend (SDL2's message hook never sees the
/// messages the shell sends, only posted ones). Null off Windows, or when that window cannot be made.
std::unique_ptr<TrayShell> MakeWin32Shell(void* mainHwnd, MessageSink sink);
/// The message the icon sends to the tray window (WM_USER + 0x51).
unsigned CallbackMessage();
/// The NIM_SETVERSION the shell accepted for the icon (4, or 0 when it refused: the old message layout).
unsigned IconVersion();
/// RegisterWindowMessageW(L"TaskbarCreated"): sent to every top-level window when Explorer (re)starts.
unsigned TaskbarCreatedMessage();
/// True while either Shift key is down (Shift + X really quits).
bool ShiftHeld();
/// Take the icon away if the launcher crashes (an unhandled exception or std::terminate), then let the earlier
/// handler run. Call once, after the shell exists.
void InstallCrashCleanup();

} // namespace okaytray
