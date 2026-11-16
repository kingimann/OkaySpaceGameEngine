#include "okay/Modeling/MeshLibrary.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace okay::modeling {

namespace fs = std::filesystem;

namespace {
std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// Read the header: "okaymesh 1", then the "verts N" and "faces M" lines (comments and blank lines are skipped).
void ReadHeader(const fs::path& p, MeshAssetInfo& info) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return;
    std::string line;
    bool header = false, gotV = false, gotF = false;
    for (int guard = 0; guard < 2000 && std::getline(f, line); ++guard) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string tag;
        if (!(ls >> tag) || tag[0] == '#') continue;
        if (!header) {
            int version = 0;
            if (tag != "okaymesh" || !(ls >> version) || version != 1) return;
            header = true;
            continue;
        }
        long long n = 0;
        if (tag == "verts" && (ls >> n) && n >= 0 && n <= 100000000) {
            info.vertices = (int)n; gotV = true;
            for (long long i = 0; i < n && f.ignore(1 << 20, '\n'); ++i) {}   // the vertex lines come next: skip them to reach "faces"
        } else if (tag == "faces" && (ls >> n) && n >= 0 && n <= 100000000) { info.faces = (int)n; gotF = true; }
        if (gotV && gotF) break;
        if (tag == "f") break;                         // faces started without a count: not a file we can describe
    }
    info.readable = header && gotV && gotF;
}
} // namespace

std::vector<MeshAssetInfo> ListMeshAssets(const std::string& root, std::size_t maxFiles, int maxDepth) {
    std::vector<MeshAssetInfo> out;
    std::error_code ec;
    const fs::path base(root);
    if (root.empty() || !fs::is_directory(base, ec)) return out;
    fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end && out.size() < maxFiles; it.increment(ec)) {
        if (it.depth() >= maxDepth) { it.disable_recursion_pending(); }
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        const fs::path p = it->path();
        std::string ext = Lower(p.extension().string());
        if (ext != ".okaymesh") continue;
        MeshAssetInfo info;
        info.path = p.string();
        info.name = p.stem().string();
        const fs::path rel = fs::relative(p.parent_path(), base, e2);
        info.folder = (e2 || rel == ".") ? std::string() : rel.generic_string();
        info.bytes = (std::uint64_t)fs::file_size(p, e2);
        ReadHeader(p, info);
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const MeshAssetInfo& a, const MeshAssetInfo& b) {
        if (a.folder != b.folder) return a.folder < b.folder;
        return Lower(a.name) < Lower(b.name);
    });
    return out;
}

std::vector<MeshAssetInfo> FilterMeshAssets(const std::vector<MeshAssetInfo>& all, const std::string& filter) {
    if (filter.empty()) return all;
    const std::string f = Lower(filter);
    std::vector<MeshAssetInfo> out;
    for (const MeshAssetInfo& a : all) if (Lower(a.name).find(f) != std::string::npos || Lower(a.folder).find(f) != std::string::npos) out.push_back(a);
    return out;
}

} // namespace okay::modeling
