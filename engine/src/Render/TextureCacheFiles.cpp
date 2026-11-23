#include "okay/Render/TextureCacheFiles.hpp"
#include "okay/Render/SoftwareRenderer.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>
#include <vector>

namespace okay {

namespace {
namespace fs = std::filesystem;

bool NotAFile(const std::string& key) { return key.empty() || key[0] == '@' || key.rfind("proc:", 0) == 0; }

// Absolute and lexically normal, with '/' separators (and lower case on Windows, where names are not case sensitive).
std::string Canon(const std::string& p) {
    std::error_code ec;
    fs::path a = fs::u8path(p);
    if (a.is_relative()) a = fs::absolute(a, ec);
    std::string s = a.lexically_normal().generic_u8string();
#ifdef _WIN32
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

std::string LastPart(const std::string& canon) {
    const std::size_t slash = canon.find_last_of('/');
    return slash == std::string::npos ? canon : canon.substr(slash + 1);
}

// Does the cache key `key` name the file whose canonical form is `want`? Lexically first; a key under another spelling
// (a link, a short name) is caught by asking the file system, only when the file names agree, so a big cache costs
// little.
bool SameFile(const std::string& key, const std::string& want, const std::string& wantName, const std::string& file) {
    const std::string k = Canon(key);
    if (k == want) return true;
    if (LastPart(k) != wantName) return false;
    std::error_code ec;
    return fs::equivalent(fs::u8path(key), fs::u8path(file), ec) && !ec;
}
} // namespace

int InvalidateTextureCacheForFile(const std::string& file) {
    if (NotAFile(file)) return 0;
    const std::string want = Canon(file);
    const std::string wantName = LastPart(want);
    int dropped = 0;
    auto& tex = TextureCacheMap();
    for (auto it = tex.begin(); it != tex.end();) {
        if (!NotAFile(it->first) && SameFile(it->first, want, wantName, file)) { it = tex.erase(it); ++dropped; }
        else ++it;
    }
    bool mips = false;
    auto& mip = MipCacheMap();
    for (auto it = mip.begin(); it != mip.end();) {
        if (!NotAFile(it->first) && SameFile(it->first, want, wantName, file)) { it = mip.erase(it); mips = true; }
        else ++it;
    }
    if (dropped > 0 || mips) ++TextureCacheGeneration();
    return dropped;
}

} // namespace okay
