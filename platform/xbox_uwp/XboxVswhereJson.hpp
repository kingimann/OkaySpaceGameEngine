#pragma once
// Pure text helpers for the Xbox SDK probe (no Windows, no files): reads the JSON that `vswhere -format json` prints (ONE call instead of four) and
// the small cache file the probe keeps. Written from vswhere's documented output (an array of objects with installationPath, installationVersion,
// displayName, ...); only those three string fields are read, everything else is skipped. UNVERIFIED against a real vswhere; tested on Linux with
// made-up text in that shape.
#include "XboxSdkCheck.hpp"
#include <string>
#include <vector>

namespace okay::xbox {

namespace detail {
// Reads a JSON string starting at s[i] == '"'; leaves i after the closing quote. Handles \\ \" \/ \n \t \r \b \f and \uXXXX (BMP to UTF-8).
inline bool JsonString(const std::string& s, std::size_t& i, std::string& out) {
    out.clear();
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c != '\\') { out += c; continue; }
        if (i >= s.size()) return false;
        char e = s[i++];
        switch (e) {
            case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break; case 'b': out += '\b'; break; case 'f': out += '\f'; break;
            case 'u': {
                if (i + 4 > s.size()) return false;
                unsigned cp = 0;
                for (int k = 0; k < 4; ++k) { char h = s[i++]; cp <<= 4; if (h >= '0' && h <= '9') cp |= unsigned(h - '0'); else if (h >= 'a' && h <= 'f') cp |= unsigned(h - 'a' + 10); else if (h >= 'A' && h <= 'F') cp |= unsigned(h - 'A' + 10); else return false; }
                if (cp < 0x80) out += char(cp);
                else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
                else { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
                break;
            }
            default: out += e; break;   // \\ \" \/
        }
    }
    return false;
}
}

/// The installs in a `vswhere -format json` array, in the order vswhere printed them. Objects without an installationPath are skipped.
/// Returns false when the text is not an array of objects (the probe then reports "vswhere gave an answer we could not read").
inline bool ParseVswhereJson(const std::string& text, std::vector<VsInstall>& out) {
    out.clear();
    std::size_t i = 0;
    auto ws = [&]() { while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n' || (unsigned char)text[i] == 0xEF || (unsigned char)text[i] == 0xBB || (unsigned char)text[i] == 0xBF)) ++i; };
    ws();
    if (i >= text.size() || text[i] != '[') return false;
    ++i;
    int depth = 0; VsInstall cur; bool inObj = false;
    while (i < text.size()) {
        ws();
        if (i >= text.size()) return false;
        char c = text[i];
        if (c == '"') {
            std::string str; if (!detail::JsonString(text, i, str)) return false;
            if (inObj && depth == 1) {   // a key of the top-level object of this install
                ws();
                if (i < text.size() && text[i] == ':') {
                    ++i; ws();
                    if (i < text.size() && text[i] == '"') {
                        std::string val; if (!detail::JsonString(text, i, val)) return false;
                        if (str == "installationPath") cur.path = val; else if (str == "installationVersion") cur.version = val; else if (str == "displayName") cur.displayName = val;
                    }
                }
            }
            continue;
        }
        ++i;
        if (c == '{') { if (depth == 0) { inObj = true; cur = VsInstall(); } ++depth; }
        else if (c == '}') { --depth; if (depth < 0) return false; if (depth == 0) { inObj = false; if (!cur.path.empty()) out.push_back(cur); } }
        else if (c == '[' && inObj) ++depth;
        else if (c == ']') { if (inObj) --depth; else return depth == 0; }
    }
    return false;
}

/// The cache file: first line the key, the rest the raw vswhere JSON. The key says which vswhere and which set of installs it was made for.
inline std::string MakeProbeCache(const std::string& key, const std::string& json) { return "okayspace-xbox-vswhere-cache-1\n" + key + "\n" + json; }
inline bool ReadProbeCache(const std::string& file, const std::string& key, std::string& json) {
    const std::string head = "okayspace-xbox-vswhere-cache-1\n";
    if (file.compare(0, head.size(), head) != 0) return false;
    const std::size_t e = file.find('\n', head.size());
    if (e == std::string::npos || file.substr(head.size(), e - head.size()) != key) return false;
    json = file.substr(e + 1);
    return !json.empty();
}

} // namespace okay::xbox
