#pragma once
// The real download behind the Versions page, through the HTTP seam (okay/Platform/NativeFetch.hpp: WinHTTP on Windows,
// our own client on Linux when on, curl only as the last fallback), plus a cancellable child-process runner. No SDL or ImGui.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "VersionsManager.hpp"

namespace okayversions::net {

struct RunResult {
    bool started = false;      ///< the program was found and started
    bool stopped = false;      ///< `keepGoing` said stop and the program was ended
    int exitCode = -1;         ///< valid when it ran to its end
};

/// Run `args[0]` with the rest as its arguments, hidden, output discarded. About every `pollMs` it calls `keepGoing()`;
/// a false answer ends the program (stopped = true). Blocks until the program has ended.
RunResult RunProcess(const std::vector<std::string>& args, const std::function<bool()>& keepGoing, int pollMs = 100);

/// Download `url` to `dest` through the HTTP seam: progress in bytes, `cancelled()` stops it, a body bigger than
/// `maxBytes` is a failure, a full disk is DiskFull. Only addresses that IsAcceptableUrl allows are fetched.
DownloadResult DownloadFile(const std::string& url, const std::filesystem::path& dest, std::uint64_t maxBytes,
                            const std::function<void(std::uint64_t)>& onBytes, const std::function<bool()>& cancelled);

}  // namespace okayversions::net
