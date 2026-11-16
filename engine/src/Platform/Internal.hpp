#pragma once
// Private to okay_platform: the backend factories (defined only when compiled in).
#include "okay/Platform/Platform.hpp"
#include <memory>
#include <string>

namespace okay::platform {
std::unique_ptr<Backend> CreateSdl2Backend();   // SdlBackend.cpp (OKAY_PLATFORM_HAVE_SDL2)
std::unique_ptr<Backend> CreateOwnBackend();    // OwnLinux.cpp / OwnWin32.cpp (OKAY_PLATFORM_HAVE_OWN)
std::unique_ptr<Window> WrapSdl2WindowImpl(void* sdlWindow);   // SdlBackend.cpp (OKAY_PLATFORM_HAVE_SDL2): non-owning view
#if defined(_WIN32)
// OwnWin32Audio.cpp: waveOut / waveIn through winmm.dll loaded at run time. Null result: `err` says why.
std::unique_ptr<AudioDevice> OpenWinmmAudio(const AudioSpec& want, bool capture, const char* name, AudioSpec* got, std::string& err);
int WinmmDeviceCount(bool capture);
std::string WinmmDeviceName(int index, bool capture);
#endif
} // namespace okay::platform
