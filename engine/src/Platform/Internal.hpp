#pragma once
// Private to okay_platform: the backend factories (defined only when compiled in).
#include "okay/Platform/Platform.hpp"
#include <memory>
#include <string>

namespace okay::platform {
std::unique_ptr<Backend> CreateSdl2Backend();   // SdlBackend.cpp (OKAY_PLATFORM_HAVE_SDL2)
std::unique_ptr<Backend> CreateOwnBackend();    // OwnLinux.cpp / OwnWin32.cpp (OKAY_PLATFORM_HAVE_OWN)

#ifdef _WIN32
namespace own {   // OwnWin32Audio.cpp (WASAPI)
std::unique_ptr<AudioDevice> OpenWasapi(const AudioSpec& want, bool capture, const char* device, AudioSpec* got, std::string& err);
int WasapiDeviceCount(bool capture);
std::string WasapiDeviceName(int index, bool capture);
} // namespace own
#endif
} // namespace okay::platform
