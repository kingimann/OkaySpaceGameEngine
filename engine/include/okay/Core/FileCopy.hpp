#pragma once
// Copy a file over one that already exists, on every platform.
//
// std::filesystem::copy_file(from, to, copy_options::overwrite_existing) is meant to replace `to`, but the MinGW C++
// library on Windows refuses when `to` exists: it decides "the same file" by comparing device and inode numbers, and
// Windows reports inode 0 for every file, so two different files on one drive look like the same one ("file exists").
// The Windows test run showed it: a project restore that failed half-way could not put the kept copies back, and a
// save thumbnail kept its old name.
//
// CopyFileOver tries the plain copy first. If that fails while `to` exists, it copies to a new file next to `to` and
// renames that over `to` (a rename does replace an existing file on Windows). Either `to` ends up a full copy of
// `from`, or it is left exactly as it was and `ec` says why. Header only; <filesystem>, no threads, no engine code, so
// shared/, launcher/ and the standalone tools include it too. Use it for every copy that may land on an existing file.
// Written from the C++17 std::filesystem specification and the behaviour seen on the Windows runners.
#include <filesystem>
#include <string>
#include <system_error>

namespace okay {

inline bool CopyFileOver(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec) {
    namespace fs = std::filesystem;
    ec.clear();
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (!ec) return true;
    std::error_code e0;
    if (!fs::exists(to, e0)) return false;   // a real failure (missing source, no folder, no permission)
    fs::path tmp = to;
    tmp += ".copytmp";
    fs::remove(tmp, e0);
    ec.clear();
    fs::copy_file(from, tmp, ec);
    if (!ec) fs::rename(tmp, to, ec);
    if (ec) { fs::remove(tmp, e0); return false; }
    return true;
}

// The same, for callers that use the throwing std::filesystem calls: throws std::filesystem::filesystem_error (with
// both paths) where the error-code form returns false.
inline void CopyFileOver(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    if (!CopyFileOver(from, to, ec)) throw std::filesystem::filesystem_error("CopyFileOver", from, to, ec);
}

}  // namespace okay
