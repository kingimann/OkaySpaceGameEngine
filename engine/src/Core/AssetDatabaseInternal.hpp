#pragma once
// Helpers shared by the AssetDatabase*.cpp files. Not part of the public API.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace okay {
namespace adb {

inline std::string Lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

inline bool StartsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}
inline bool EndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// UTF-8 <-> filesystem paths, the same under C++17 (std::string) and C++20 (std::u8string).
inline std::filesystem::path PathFromU8(const std::string& s) {
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    return std::filesystem::u8path(s);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
}
inline std::string U8(const std::filesystem::path& p) {
    const auto s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

/// Read a whole file up to `maxBytes` (false when it can not be read or is bigger).
inline bool ReadAll(const std::filesystem::path& p, std::size_t maxBytes, std::string& out) {
    out.clear();
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0 || static_cast<unsigned long long>(size) > maxBytes) return false;
    f.seekg(0);
    out.resize(static_cast<std::size_t>(size));
    if (size > 0) f.read(&out[0], size);
    if (!f && size > 0) { out.clear(); return false; }
    return true;
}

/// How a reference found in a text asset was matched to a file.
enum RefStyle { kStyleRoot = 0, kStyleRelative = 1 };

} // namespace adb
} // namespace okay
