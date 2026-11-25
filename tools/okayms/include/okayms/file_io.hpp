#pragma once

// Whole-file reads and writes for the okay-ms command-line drivers (okay-cl, okay-link, okay-lib) and tests: one copy
// of these helpers for all of them (docs/engine/shared_code.md).

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace okayms {

inline bool file_exists(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    return static_cast<bool>(in);
}

// The file's bytes; on failure *error is set and the result is empty.
inline std::vector<uint8_t> read_file(const std::string& path, std::string* error) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        *error = "cannot open " + path;
        return {};
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline bool write_file(const std::string& path, const std::vector<uint8_t>& bytes, std::string* error) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) {
        *error = "cannot write " + path;
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        *error = "cannot write " + path;
        return false;
    }
    return true;
}

}  // namespace okayms
