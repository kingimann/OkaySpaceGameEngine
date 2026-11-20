#pragma once
// Real transports for the content downloader. Docs: docs/content_delivery.md "Transports".
//   * HttpTransport: GET over the engine's own HTTP layer (okay/Platform/NativeHttp.hpp): WinHTTP on Windows, our own HTTP/1.1 client on
//     Linux when built with OKAY_OWN_HTTP and run with OKAY_OWN_HTTP=1. No curl, no third-party SDK. Each GET is written to a scratch file
//     (so a big file never sits in memory) and handed to the sync engine in pieces. The Range header is sent as an ordinary header.
//   * UwpTransportStub: the Xbox / UWP build. It does NOT download anything yet; it fails with a plain message. See the note on the class.
//   * MakeTransport(url, scratch): file:// -> FileTransport, http(s):// -> HttpTransport (UwpTransportStub when OKAY_XBOX_UWP is defined).
// Header only; no threads (Windows build rule), no <mutex>.
#include <atomic>
#include <memory>

#include "ContentTransport.hpp"
#include "okay/Platform/NativeHttp.hpp"

namespace okaycontent {

class HttpTransport : public Transport {
public:
    /// `scratchDir` must exist and be writable (the sync's own content folder is fine). `timeoutMs` only applies on the own client.
    HttpTransport(std::string baseUrl, std::string scratchDir, int timeoutMs = 30000) : m_base(std::move(baseUrl)), m_scratch(std::move(scratchDir)), m_timeoutMs(timeoutMs) {}

    GetResult Get(const std::string& name, std::uint64_t rangeFrom, std::uint64_t maxBytes, const GetHandlers& h) override {
        GetResult r;
        namespace fs = std::filesystem;
        if (!okay::http::NativeAvailable()) {
            r.error = "no built-in HTTP client is available here (Windows uses WinHTTP; on Linux build with OKAY_OWN_HTTP and set OKAY_OWN_HTTP=1)";
            return r;
        }
        static std::atomic<unsigned> counter{0};
        std::error_code ec;
        fs::create_directories(fs::u8path(m_scratch), ec);
        const fs::path tmp = fs::u8path(m_scratch) / ("dl-" + std::to_string(++counter) + ".tmp");
        std::vector<std::string> headers;   // (the own client sets its own Accept-Encoding and refuses one given here)
        if (rangeFrom > 0) headers.push_back("Range: bytes=" + std::to_string(rangeFrom) + "-");
        const okay::http::Result hr = okay::http::Request("GET", JoinUrl(m_base, name), headers, std::string(), std::string(), tmp.string(), m_timeoutMs);
        if (!hr.reached) {
            r.error = hr.error.empty() ? "could not reach the server" : hr.error;
            fs::remove(tmp, ec);
            return r;
        }
        GetHead head;
        head.status = static_cast<int>(hr.status);
        // A 206 answer to "bytes=N-" begins at N (RFC 9110). The Content-Range header is not exposed by the Windows client, so this is
        // assumed; the sync engine verifies the final size and SHA-256 of the whole file, so a server that lies fails closed.
        head.bodyStart = hr.status == 206 ? rangeFrom : 0;
        if (h.onHead && !h.onHead(head)) { r.aborted = true; fs::remove(tmp, ec); return r; }
        if (hr.status == 200 || hr.status == 206) {
            std::ifstream in(tmp, std::ios::binary);
            if (!in) { r.error = "could not read the downloaded data"; fs::remove(tmp, ec); return r; }
            std::vector<char> buf(64 * 1024);
            std::uint64_t total = 0;
            while (in) {
                in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
                const std::streamsize n = in.gcount();
                if (n <= 0) break;
                total += static_cast<std::uint64_t>(n);
                if (maxBytes && total > maxBytes) { r.error = "the body is larger than allowed"; in.close(); fs::remove(tmp, ec); return r; }
                if (h.onData && !h.onData(reinterpret_cast<const std::uint8_t*>(buf.data()), static_cast<std::size_t>(n))) { r.aborted = true; in.close(); fs::remove(tmp, ec); return r; }
            }
        }
        r.reached = true;
        fs::remove(tmp, ec);
        return r;
    }
    const char* Describe() const override { return "http"; }

private:
    std::string m_base, m_scratch;
    int m_timeoutMs;
};

/// The Xbox / UWP transport. NOT IMPLEMENTED: it only fails, with a plain message, so a UWP build never pretends to download.
/// What it needs (UNVERIFIED, nothing here has run on an Xbox):
///   * a real GET: Windows.Web.Http.HttpClient (WinRT) from the UWP glue, or our own HTTP over Winsock (both are allowed APIs; Winsock needs
///     the same capability); WinHTTP itself is NOT available to UWP apps, which is why HttpTransport above cannot be used there;
///   * the "internetClient" capability in Package.appxmanifest: platform/xbox_uwp/XboxManifest.hpp lists it by default (branch
///     claude/xbox-uwp-2, tested there) and it must stay in the generated manifest of any game that sets a content URL;
///   * the content folder under ApplicationData.LocalFolder (XboxPaths: install folder is read-only), never the install folder.
/// Content is data and scripts only; a UWP app must never download and run code, and nothing here can.
class UwpTransportStub : public Transport {
public:
    GetResult Get(const std::string&, std::uint64_t, std::uint64_t, const GetHandlers&) override {
        GetResult r;
        r.error = "Downloading game content is not available in the Xbox app yet (the Xbox network transport is not written).";
        return r;
    }
    const char* Describe() const override { return "uwp-stub"; }
};

/// The Xbox / UWP build plugs its real transport in here (platform/xbox_uwp/XboxContentTransport.hpp over a Windows.Web.Http client): set once at start-up,
/// before the first MakeTransport. While it is empty the stub above is used. Plain function, no threads.
inline std::function<std::unique_ptr<Transport>(const std::string& url)>& UwpTransportFactory() {
    static std::function<std::unique_ptr<Transport>(const std::string&)> f;
    return f;
}

/// "" url -> nullptr. file://folder -> FileTransport. http(s):// -> HttpTransport, or the UWP stub when OKAY_XBOX_UWP is defined.
inline std::unique_ptr<Transport> MakeTransport(const std::string& url, const std::string& scratchDir) {
    if (url.empty()) return nullptr;
    const std::string folder = FileUrlFolder(url);
    if (!folder.empty()) return std::make_unique<FileTransport>(folder);
    if (url.compare(0, 8, "https://") == 0 || url.compare(0, 7, "http://") == 0) {
#if defined(OKAY_XBOX_UWP)
        (void)scratchDir;
        if (UwpTransportFactory()) { if (auto t = UwpTransportFactory()(url)) return t; }
        return std::make_unique<UwpTransportStub>();
#else
        return std::make_unique<HttpTransport>(url, scratchDir);
#endif
    }
    return nullptr;
}

}  // namespace okaycontent
