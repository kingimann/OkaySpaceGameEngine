// okay-ms own <windows.h>: the Win32 declarations the engine uses, written in our own words from Microsoft's public
// Win32 API documentation and linked against the system DLLs (kernel32.dll, user32.dll). No text from the Windows SDK,
// MinGW or Wine headers. It is split by area under okay_win32/ so it can grow; add a declaration to the area it belongs
// to when an engine file needs it.
//
// Differences from the SDK's header, on purpose:
//   - no min and max macros (as if NOMINMAX were always defined) and no A/W name macros (CreateFile, MessageBox, ...):
//     the engine calls the W or A function by its full name;
//   - WIN32_LEAN_AND_MEAN changes nothing (there is nothing extra to leave out yet).
#pragma once
#include "okay_win32/base.h"
#include "okay_win32/errors.h"
#include "okay_win32/sync.h"
#include "okay_win32/process.h"
#include "okay_win32/files.h"
#include "okay_win32/system.h"
#include "okay_win32/modules.h"
#include "okay_win32/strings.h"
#include "okay_win32/memory.h"
#include "okay_win32/user.h"
