#pragma once
// Turn a "text/uri-list" (RFC 2483: one URI per line, CRLF, lines starting with '#' are comments) into local file paths.
// Used by the own X11 backend for XDND file drops; plain C++ so it is tested everywhere. Written from the RFC and RFC 3986
// percent-encoding, nothing else.
#include <string>
#include <vector>

namespace okay::platform {

/// "%41" -> "A". A '%' not followed by two hex digits is kept as written. Returns false (and an empty string) when the
/// decoded text would contain a NUL byte (%00), which no path can.
inline bool PercentDecode(const std::string& in, std::string& out) {
    out.clear();
    auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() && hex(in[i + 1]) >= 0 && hex(in[i + 2]) >= 0) {
            const int v = hex(in[i + 1]) * 16 + hex(in[i + 2]);
            if (v == 0) { out.clear(); return false; }
            out += (char)v; i += 2;
        } else out += in[i];
    }
    return true;
}

/// The local paths named by `data`: "file:///a%20b" and "file://localhost/a" give "/a b" and "/a". A URI with another scheme, a
/// remote host, no path, a NUL, or a relative path is skipped. An empty line or comment is skipped.
inline std::vector<std::string> UriListToPaths(const std::string& data) {
    std::vector<std::string> paths;
    size_t pos = 0;
    while (pos <= data.size()) {
        size_t end = data.find('\n', pos);
        if (end == std::string::npos) end = data.size();
        std::string line = data.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        static const char kScheme[] = "file://";
        if (line.size() < sizeof(kScheme) - 1) continue;
        std::string scheme = line.substr(0, sizeof(kScheme) - 1);
        for (char& c : scheme) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (scheme != kScheme) continue;
        const std::string rest = line.substr(sizeof(kScheme) - 1);
        const size_t slash = rest.find('/');
        if (slash == std::string::npos) continue;                 // no path
        const std::string host = rest.substr(0, slash);
        if (!host.empty() && host != "localhost") continue;       // a file on another machine
        std::string path;
        if (!PercentDecode(rest.substr(slash), path) || path.empty() || path[0] != '/') continue;
        paths.push_back(path);
    }
    return paths;
}

} // namespace okay::platform
