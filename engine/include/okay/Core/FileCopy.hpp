#pragma once
// Copy a file over one that already exists, on every platform.
//
// std::filesystem::copy_file(from, to, copy_options::overwrite_existing) is meant to replace `to`, but the MinGW C++
// library on Windows refuses when `to` exists: it decides "the same file" by comparing device and inode numbers, and
// Windows reports inode 0 for every file, so two different files on one drive look like the same one ("file exists").
// The Windows test run showed it: a project restore that failed half-way could not put the kept copies back, and a
// save thumbnail kept its old name.
//
// CopyFileOver never writes into `to` itself. When `to` does not exist it is a plain copy. When `to` exists, it copies
// to a new file next to `to` and renames that over `to` (a rename does replace an existing file on Windows, Linux and
// macOS). So either `to` ends up a full copy of `from`, or it is left exactly as it was and `ec` says why: a copy that
// dies half-way (full disk, missing source, no permission) cannot leave a truncated target. Copying a file onto itself
// keeps it whole. Header only; <filesystem>, no threads, no engine code, so shared/, launcher/ and the standalone
// tools include it too. Use it for every copy that may land on an existing file.
// Written from the C++17 std::filesystem specification and the behaviour seen on the Windows runners.
#include <filesystem>
#include <string>
#include <system_error>

namespace okay {

inline bool CopyFileOver(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec) {
    namespace fs = std::filesystem;
    ec.clear();
    std::error_code e0;
    if (!fs::exists(to, e0)) {
        fs::copy_file(from, to, fs::copy_options::none, ec);
        if (ec) fs::remove(to, e0);   // do not leave a half-written new file behind
        return !ec;
    }
    fs::path tmp = to;
    tmp += ".copytmp";
    fs::remove(tmp, e0);
    fs::copy_file(from, tmp, fs::copy_options::none, ec);
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
