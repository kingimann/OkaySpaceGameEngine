#pragma once
// Installing the UWP backend as okay::platform::Default() (see UwpPlatform.cpp). UNVERIFIED on Windows.
#include "okay/Platform/Platform.hpp"
namespace okay::xbox {
/// Make `b` the backend okay::platform::Default() / Get(Own) return (nullptr removes it). The caller keeps ownership.
void InstallBackend(platform::Backend* b);
platform::Backend* InstalledBackend();
}
