#pragma once
// Opening a model file by a UTF-8 path. Paths in the engine are UTF-8 strings; on Windows a narrow fopen() / ifstream
// reads them through the ANSI code page, so a folder or file name with a character outside it ("Müller", "立方") cannot be
// opened. Internal: not installed.
#include "okay/Core/Utf8.hpp"
#include <cstdio>
#include <string>

namespace okay {
namespace detail {

inline std::FILE* OpenRead(const std::string& utf8Path) {
#ifdef _WIN32
    return _wfopen(utf8::ToWide(utf8Path).c_str(), L"rb");
#else
    return std::fopen(utf8Path.c_str(), "rb");
#endif
}

} // namespace detail
} // namespace okay
