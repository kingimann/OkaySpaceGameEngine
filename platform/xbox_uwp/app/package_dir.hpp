// The package install folder (read-only), without any WinRT call: GetCurrentPackagePath (appmodel.h, available to UWP apps) works before
// CoreApplication::Run has set up the apartment, which wWinMain needs for its "is there game data?" check. Included after <windows.h>.
// EXPERIMENTAL, UNCOMPILED, UNVERIFIED.
#pragma once
#include <appmodel.h>
#include <string>
#include <vector>

namespace okay_xbox_app {
inline std::wstring PackageDirWide() {
    UINT32 len = 0;
    if (GetCurrentPackagePath(&len, nullptr) != ERROR_INSUFFICIENT_BUFFER || len == 0) return std::wstring();
    std::vector<wchar_t> buf(len);
    if (GetCurrentPackagePath(&len, buf.data()) != ERROR_SUCCESS) return std::wstring();
    return std::wstring(buf.data());
}
}
