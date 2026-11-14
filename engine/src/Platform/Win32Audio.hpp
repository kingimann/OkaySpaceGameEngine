#pragma once
// Windows audio devices for the own backend (waveOut / waveIn: no COM, available on every Windows version,
// the format mapper converts rates and channel counts so the device runs at what was asked for).
#include "okay/Platform/Platform.hpp"
#include <memory>
#include <string>

namespace okay::platform::win32 {
std::unique_ptr<AudioDevice> OpenWaveDevice(const AudioSpec& want, bool capture, const char* deviceName, AudioSpec* got, std::string& err);
int WaveDeviceCount(bool capture);
std::string WaveDeviceName(int index, bool capture);   // the driver's name, cut by Windows to 31 characters
} // namespace okay::platform::win32
