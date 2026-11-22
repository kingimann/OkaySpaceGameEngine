#pragma once
// A file's size and modification time in one call, for the "has this file changed?" checks: the asset hot reload
// (AssetManager), the audio clip cache (AudioDecode) and the baked nav mesh (NavMeshSurface). Each kept its own copy of
// these few lines until 2026-11 (#422 audit item 26); each still applies its own rule for a missing piece, from the
// flags below. Uses std::filesystem with error codes only (never throws); the path is UTF-8.
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace okay {

struct FileStamp {
    bool          sizeOk = false;   ///< `size` was read
    bool          timeOk = false;   ///< `time` was read
    std::uintmax_t size = 0;        ///< bytes (0 when not read)
    std::int64_t   time = 0;        ///< last write time in the file clock's ticks (0 when not read)
};

inline FileStamp StampOf(const std::string& utf8Path) {
    FileStamp s;
    std::error_code ec;
    const std::filesystem::path p = std::filesystem::u8path(utf8Path);
    const std::uintmax_t sz = std::filesystem::file_size(p, ec);
    if (!ec) { s.size = sz; s.sizeOk = true; }
    const auto t = std::filesystem::last_write_time(p, ec);
    if (!ec) { s.time = (std::int64_t)t.time_since_epoch().count(); s.timeOk = true; }
    return s;
}

} // namespace okay
