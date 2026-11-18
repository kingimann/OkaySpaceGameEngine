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

// Reserve `bytes` for the whole of an open file (when it is not larger than `cap`) and go back to its start, so that
// reading it in chunks does not grow the string by doubling: that held up to twice the file's size at once.
inline void ReserveForFile(std::FILE* fp, std::string& bytes, std::size_t cap) {
    if (!fp || std::fseek(fp, 0, SEEK_END) != 0) return;
    const long sz = std::ftell(fp);
    if (sz > 0 && (unsigned long)sz <= cap) bytes.reserve((std::size_t)sz);
    std::rewind(fp);
}

} // namespace detail
} // namespace okay
