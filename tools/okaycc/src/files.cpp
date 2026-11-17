#include "files.hpp"

#include <fstream>
#include <sstream>
#include <vector>

namespace okaycc {

std::string normalizePath(const std::string& path) {
    if (path.empty()) return ".";
    bool abs = path[0] == '/';
    std::vector<std::string> parts;
    std::string cur;
    auto flush = [&]() {
        if (cur.empty() || cur == ".") {
        } else if (cur == "..") {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.push_back(cur);
        }
        cur.clear();
    };
    for (char c : path) {
        if (c == '/') flush();
        else cur.push_back(c);
    }
    flush();
    if (parts.empty()) return abs ? "/" : ".";
    std::string out = abs ? "/" : "";
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += "/";
        out += parts[i];
    }
    return out;
}

std::string dirOf(const std::string& path) {
    std::string n = normalizePath(path);
    auto p = n.find_last_of('/');
    if (p == std::string::npos) return "";
    if (p == 0) return "/";
    return n.substr(0, p);
}

std::string joinPath(const std::string& dir, const std::string& rel) {
    if (!rel.empty() && rel[0] == '/') return normalizePath(rel);
    if (dir.empty() || dir == ".") return normalizePath(rel);
    return normalizePath(dir + "/" + rel);
}

void MemoryFiles::add(std::string path, std::string contents) {
    files_[normalizePath(path)] = std::move(contents);
}

bool MemoryFiles::read(const std::string& path, std::string& out) const {
    auto it = files_.find(normalizePath(path));
    if (it != files_.end()) {
        out = it->second;
        return true;
    }
    return DiskFiles{}.read(path, out);
}

bool DiskFiles::read(const std::string& path, std::string& out) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

}  // namespace okaycc
