#pragma once
// Version lists for tests: one entry of a versions.json (the releases' or the test builds' dev/versions.json, the same schema 1), so the tests of
// the Versions page model (tests/test_launcher_versions.cpp) and of the page drawn headless (okayui/tests/test_okayui_port_versions.cpp) build
// their lists one way. Only test data: sizes and checksums are made up.
#include <string>
#include <vector>

namespace okaytestlists {

/// The "files" object of an entry: the editor (with or without a SHA-256), the player and the launcher, each with a size and `sha`.
inline std::string Files(const std::string& sha, bool editorSha = true, unsigned long long editorSize = 1000) {
    return "{\"OkayEngine.exe\":{\"size\":" + std::to_string(editorSize) + (editorSha ? ",\"sha256\":\"" + sha + "\"" : std::string()) +
           "},\"OkaySpacePlayer.exe\":{\"size\":500,\"sha256\":\"" + sha + "\"},\"OkaySpace.exe\":{\"size\":300,\"sha256\":\"" + sha + "\"}}";
}
/// One entry. `extra` is put in as it is (",\"channel\":\"latest\"", ",\"branch\":\"claude/x\"").
inline std::string Entry(const std::string& version, const std::string& date, bool inBucket, const std::string& files, const std::string& extra = "") {
    return "{\"version\":\"" + version + "\",\"date\":\"" + date + "\",\"inBucket\":" + (inBucket ? "true" : "false") + extra + ",\"files\":" + files + "}";
}
/// The whole list: schema 1, `head` ("" = none) as "latest", the entries.
inline std::string List(const std::vector<std::string>& entries, const std::string& head = "") {
    std::string out = "{\"schema\":1" + (head.empty() ? std::string() : ",\"latest\":\"" + head + "\"") + ",\"versions\":[";
    for (std::size_t i = 0; i < entries.size(); ++i) out += (i ? "," : "") + entries[i];
    return out + "]}";
}

}  // namespace okaytestlists
