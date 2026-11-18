#pragma once
// Drop-in adapter: an okaycontent::Transport (shared/ContentTransport.hpp of the branch claude/content-delivery-1, NOT merged here) that downloads with the
// pure HTTP driver (XboxHttp.hpp) over any okay::xbox::http::Client (the Windows.Web.Http glue, app/xbox_winhttp.hpp, in the app). It compiles only when
// ContentTransport.hpp is on the include path (`__has_include`), so this branch builds without that one. When the branch lands, MakeTransport's
// UwpTransportStub is replaced by `UwpHttpTransport` (one line there). Header only, no threads. UNVERIFIED on a console.
#include "XboxHttp.hpp"

#if defined(__has_include)
#  if __has_include("ContentTransport.hpp")
#    include "ContentTransport.hpp"
#    define OKAY_XBOX_HAVE_CONTENT_TRANSPORT 1
#  endif
#endif

#if defined(OKAY_XBOX_HAVE_CONTENT_TRANSPORT)
namespace okay::xbox {

class UwpHttpTransport : public okaycontent::Transport {
public:
    /// `baseUrl` e.g. "https://cdn.example.com/mygame" (https; plain http only with `opt.allowHttp`). The client is owned by the caller and must outlive this.
    UwpHttpTransport(http::Client& client, std::string baseUrl, http::Options opt = http::Options()) : client_(client), base_(std::move(baseUrl)), opt_(std::move(opt)) {}
    okaycontent::GetResult Get(const std::string& name, std::uint64_t rangeFrom, std::uint64_t maxBytes, const okaycontent::GetHandlers& h) override {
        http::GetHandlers hh;
        if (h.onHead) hh.onHead = [&h](const http::GetHead& x) { okaycontent::GetHead y; y.status = x.status; y.bodyStart = x.bodyStart; y.totalSize = x.totalSize; return h.onHead(y); };
        if (h.onData) hh.onData = h.onData;
        const http::GetResult r = http::Get(client_, base_, name, rangeFrom, maxBytes, hh, opt_);
        okaycontent::GetResult out; out.reached = r.reached; out.aborted = r.aborted; out.error = r.error; return out;
    }
    const char* Describe() const override { return "uwp-http"; }
private:
    http::Client& client_; std::string base_; http::Options opt_;
};

} // namespace okay::xbox
#endif
