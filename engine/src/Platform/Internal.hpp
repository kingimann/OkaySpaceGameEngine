#pragma once
// Private to okay_platform: the backend factories (defined only when compiled in).
#include "okay/Platform/Platform.hpp"
#include <memory>

namespace okay::platform {
std::unique_ptr<Backend> CreateSdl2Backend();   // SdlBackend.cpp (OKAY_PLATFORM_HAVE_SDL2)
std::unique_ptr<Backend> CreateOwnBackend();    // OwnLinux.cpp / OwnWin32.cpp (OKAY_PLATFORM_HAVE_OWN)
std::unique_ptr<Window> WrapSdl2WindowImpl(void* sdlWindow);   // SdlBackend.cpp (OKAY_PLATFORM_HAVE_SDL2): non-owning view
} // namespace okay::platform
