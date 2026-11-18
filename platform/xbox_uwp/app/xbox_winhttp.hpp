// The Windows.Web.Http glue for XboxHttp.hpp's Client interface (C++/WinRT, UWP). EXPERIMENTAL, UNCOMPILED, UNVERIFIED: never through MSVC, never run.
// Written from Microsoft's documentation of Windows.Web.Http.HttpClient (SendRequestAsync with HttpCompletionOption::ResponseHeadersRead),
// HttpResponseMessage (StatusCode, Headers, Content().Headers()), IHttpContent.ReadAsInputStreamAsync and Windows.Storage.Streams.DataReader. Needs the
// `internetClient` capability in the manifest (the generated manifest has it). The package folder is read-only; downloads belong in LocalFolder.
//
// BLOCKING: Send() waits for the network on the calling thread with .get(), which C++/WinRT refuses on a UI (ASTA) thread. Call it from a worker thread
// (the content sync's own thread), never from the CoreWindow loop. Plain http is refused here as well unless the pure driver allowed it (Options::allowHttp).
// Not wired into the engine bridge yet: the content downloader that would call it is on the branch claude/content-delivery-1 (not merged).
#pragma once
#if defined(OKAY_XBOX_APP_ENGINE) || defined(OKAY_XBOX_WINHTTP)
#include "XboxHttp.hpp"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>

#include <string>
#include <vector>

namespace okay_xbox_app {

class WinRtHttpClient : public okay::xbox::http::Client {
public:
    okay::xbox::http::ClientResult Send(const okay::xbox::http::Request& rq, const okay::xbox::http::Sink& sink) override {
        namespace wwh = winrt::Windows::Web::Http;
        namespace wss = winrt::Windows::Storage::Streams;
        okay::xbox::http::ClientResult out;
        try {
            // Redirects are followed by the pure driver (it checks https-to-http and the count), so the client must not follow them itself.
            wwh::Filters::HttpBaseProtocolFilter filter; filter.AllowAutoRedirect(false);
            wwh::HttpClient client(filter);
            wwh::HttpRequestMessage req(wwh::HttpMethod::Get(), winrt::Windows::Foundation::Uri(winrt::to_hstring(rq.url)));
            for (const auto& h : rq.headers) req.Headers().TryAppendWithoutValidation(winrt::to_hstring(h.name), winrt::to_hstring(h.value));
            wwh::HttpResponseMessage resp = client.SendRequestAsync(req, wwh::HttpCompletionOption::ResponseHeadersRead).get();
            okay::xbox::http::Head head; head.status = static_cast<int>(resp.StatusCode());
            for (auto const& kv : resp.Headers()) head.headers.push_back({winrt::to_string(kv.Key()), winrt::to_string(kv.Value())});
            for (auto const& kv : resp.Content().Headers()) head.headers.push_back({winrt::to_string(kv.Key()), winrt::to_string(kv.Value())});   // Content-Length, Content-Range
            if (!sink.onHead(head)) { out.aborted = true; resp.Close(); return out; }
            wss::IInputStream in = resp.Content().ReadAsInputStreamAsync().get();
            wss::DataReader reader(in);
            reader.InputStreamOptions(wss::InputStreamOptions::Partial);
            for (;;) {
                const uint32_t n = reader.LoadAsync(64 * 1024).get();
                if (n == 0) break;
                std::vector<uint8_t> buf(n); reader.ReadBytes(buf);
                if (!sink.onData(buf.data(), buf.size())) { out.aborted = true; resp.Close(); return out; }
            }
            out.reached = true; resp.Close();
        } catch (winrt::hresult_error const& e) { out.error = "network error: " + winrt::to_string(e.message()); }
        catch (std::exception const& e) { out.error = std::string("network error: ") + e.what(); }
        return out;
    }
};

} // namespace okay_xbox_app
#endif
