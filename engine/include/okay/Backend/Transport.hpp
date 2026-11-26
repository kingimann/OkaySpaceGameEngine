// OkayBackend transport: one request in, one answer out (docs/platform/okay_backend.md, section 3).
//
// A provider (okay/Backend/Supabase.hpp today) turns a call such as "sign in" into a Request; a Transport delivers it:
//   HttpTransport       the network, through the HTTP seam okay::http::Send (WinHTTP on Windows, our own client elsewhere)
//   HookTransport       a test function in front of the network (AccountService::SetTransportForTest)
//   RecordingTransport  tests: keeps every request and answers from a function, so no request leaves the process
//
// Written from RFC 9110 (HTTP semantics: methods, header fields, status classes) and the HTTP seam's own contract
// (NativeFetch.hpp). Header-only and free of <mutex> / <thread> (CLAUDE.md rule 21a): the launcher, editor and player include it.
#pragma once

#include "okay/Platform/NativeFetch.hpp"   // okay::http::Send, okay::http::Result

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace okay::backend {

/// One request as it goes on the wire: the method, the full address, the header lines in the order they are sent
/// ("Name: value"), and either a body or a file whose bytes are the body (uploads). `timeoutSec` > 0 limits the whole call.
struct Request {
    std::string method;
    std::string url;
    std::vector<std::string> headers;
    std::string body;
    std::string bodyFile;
    int timeoutSec = 0;
};

/// The answer. `ok` is a 2xx status; `reached` is false when no HTTP answer came (offline, DNS, TLS, timeout), and then
/// `error` holds the seam's low-level reason (never a secret) and `maybeSent` whether the request may have arrived (such a
/// request is never sent again). This is AccountService's ApiResponse (okay::account::ApiResponse is this type).
struct Response {
    bool        ok      = false;
    bool        reached = false;
    long        status  = 0;
    std::string body;
    std::string error;
    bool        maybeSent = false;
};

/// The seam's answer as a Response: any status is an answer; no answer keeps the reason ("no answer" when it gave none).
inline Response FromHttp(okay::http::Result nr) {
    Response out;
    if (!nr.reached) {
        out.error = nr.error.empty() ? std::string("no answer") : nr.error;
        out.maybeSent = nr.maybeSent;
        return out;
    }
    out.status = nr.status;
    out.body = std::move(nr.body);
    out.reached = true;
    out.ok = out.status >= 200 && out.status < 300;
    return out;
}

/// Where requests go. Send never sends a request twice by itself.
class Transport {
public:
    virtual ~Transport() = default;
    virtual Response Send(const Request& request) = 0;
};

/// The network, through okay::http::Send. A body file is sent as the body when one is given.
class HttpTransport final : public Transport {
public:
    Response Send(const Request& r) override {
        return FromHttp(okay::http::Send(r.method, r.url, r.headers, r.bodyFile.empty() ? r.body : std::string(), r.bodyFile,
                                         r.timeoutSec > 0 ? r.timeoutSec * 1000 : 0));
    }
};

/// A test function (method, url, header lines, body) answers instead of the network while one is set; with none, the
/// network. The body is empty for a file upload (the function never sees the file). AccountService's transport.
class HookTransport final : public Transport {
public:
    using Hook = std::function<okay::http::Result(const std::string&, const std::string&, const std::vector<std::string>&,
                                                  const std::string&)>;
    void SetHook(Hook h) { hook_ = std::move(h); }
    bool HasHook() const { return static_cast<bool>(hook_); }
    Response Send(const Request& r) override {
        if (!hook_) return http_.Send(r);
        return FromHttp(hook_(r.method, r.url, r.headers, r.bodyFile.empty() ? r.body : std::string()));
    }

private:
    Hook hook_;
    HttpTransport http_;
};

/// Tests: every request is kept in `sent` (in order) and answered by `answer` (no answer when it is empty). Nothing
/// touches the network.
class RecordingTransport final : public Transport {
public:
    std::vector<Request> sent;
    std::function<Response(const Request&)> answer;
    Response Send(const Request& r) override {
        sent.push_back(r);
        if (!answer) { Response none; none.error = "recording transport: no answer"; return none; }
        return answer(r);
    }
};

} // namespace okay::backend
