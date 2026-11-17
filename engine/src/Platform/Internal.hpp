#pragma once
// Private to okay_platform: the backend factories (defined only when compiled in).
#include "okay/Platform/Platform.hpp"
#include <memory>
#include <string>
#include <vector>

namespace okay::platform {
std::unique_ptr<Backend> CreateSdl2Backend();   // SdlBackend.cpp (OKAY_PLATFORM_HAVE_SDL2)
std::unique_ptr<Backend> CreateOwnBackend();    // OwnLinux.cpp / OwnWin32.cpp (OKAY_PLATFORM_HAVE_OWN)
std::unique_ptr<Window> WrapSdl2WindowImpl(void* sdlWindow);   // SdlBackend.cpp (OKAY_PLATFORM_HAVE_SDL2): non-owning view
bool TranslateSdl2EventImpl(const void* sdlEvent, Event& out);  // SdlBackend.cpp (OKAY_PLATFORM_HAVE_SDL2)
/// Platform.cpp: a mode list as Backend::DisplayModes returns it (no empty sizes, largest first, no repeats).
std::vector<VideoMode> SortedVideoModes(std::vector<VideoMode> modes);
/// Platform.cpp: the index in `displays` of the display holding the centre of (x, y, w, h), else the one it overlaps most; -1 = none.
int DisplayIndexForRect(const std::vector<DisplayInfo>& displays, int x, int y, int w, int h);
#if defined(_WIN32)
// OwnWin32Audio.cpp: waveOut / waveIn through winmm.dll loaded at run time. Null result: `err` says why.
std::unique_ptr<AudioDevice> OpenWinmmAudio(const AudioSpec& want, bool capture, const char* name, AudioSpec* got, std::string& err);
int WinmmDeviceCount(bool capture);
std::string WinmmDeviceName(int index, bool capture);
#endif
} // namespace okay::platform
