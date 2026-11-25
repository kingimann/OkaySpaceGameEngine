#pragma once
// The Win32 side of the launcher's tray icon (launcher/TrayLogic.hpp TrayShell): Shell_NotifyIconW, the tray menu,
// hiding and showing the window. Built on Windows only (launcher/TrayWin32.cpp); nothing here includes <windows.h>.
#include "TrayLogic.hpp"
#include <memory>

namespace okaytray {

/// A shell for the launcher's main window (its HWND). Null off Windows.
std::unique_ptr<TrayShell> MakeWin32Shell(void* hwnd);
/// The message the icon sends to the window (WM_USER + 0x51; the low word of lParam is the mouse or NIN_ message).
unsigned CallbackMessage();
/// RegisterWindowMessageW(L"TaskbarCreated"): sent to every top-level window when Explorer (re)starts.
unsigned TaskbarCreatedMessage();
/// True while either Shift key is down (Shift + X really quits).
bool ShiftHeld();
/// Take the icon away if the launcher crashes (an unhandled exception or std::terminate), then let the earlier
/// handler run. Call once, after the shell exists.
void InstallCrashCleanup(void* hwnd);

} // namespace okaytray
