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

/// Non-XInput game controllers through Raw Input and HidP_* (OwnWin32Hid.cpp). Everything runs on the thread that
/// pumps window messages. Layout and value maths are in Win32Hid.hpp (tested on Linux).
class RawPads {
public:
    struct Change { bool added; int instanceId; int index; };   // index among the raw pads (for an added one)
    RawPads();
    ~RawPads();
    RawPads(const RawPads&) = delete;
    RawPads& operator=(const RawPads&) = delete;
    /// RegisterRawInputDevices for joysticks and game pads, delivered to `hwnd` even when it is not focused. Also lists the pads already plugged in.
    bool Register(void* hwnd);
    void OnInput(void* hRawInput);                      // WM_INPUT: lParam
    void OnDeviceChange(unsigned wParam, void* hDevice); // WM_INPUT_DEVICE_CHANGE
    int Count() const;
    bool PopChange(Change& c);
    std::unique_ptr<Gamepad> Open(int index);           // index among the raw pads, 0..Count()-1
    struct Impl;
private:
    Impl* m;
};
} // namespace own
#endif
} // namespace okay::platform
