// Shared pieces of the OkayBackend golden request tests (tests/test_okay_backend_golden_auth.cpp, round 2, and
// tests/test_okay_backend_golden_db.cpp, round 3; docs/platform/okay_backend.md section 10.1): a fake server that writes
// down every request in one text form and answers from a queue, made-up tokens, and reading the request blocks back out of a
// golden text. No request ever leaves the process. The address, key and tokens are made up.
#pragma once

#include "okay/Core/Base64.hpp"
#include "okay/Platform/NativeFetch.hpp"   // okay::http::Result

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace golden {

inline const char* const kUrl = "https://backend.example.test/";   // trailing slash on purpose: every flow must drop it
inline const char* const kKey = "test-publishable-key-0000";
inline const long kFuture = 4102444800L;   // 2100-01-01

inline std::string B64Url(const std::string& in) {
    return okay::base64::Encode(reinterpret_cast<const std::uint8_t*>(in.data()), in.size(), okay::base64::Alphabet::Url, false);
}
/// JWT-shaped, unsigned: the client only reads "sub" and "exp".
inline std::string Jwt(const std::string& sub, long exp) {
    return B64Url("{\"alg\":\"none\"}") + "." + B64Url("{\"sub\":\"" + sub + "\",\"exp\":" + std::to_string(exp) + "}") + ".sig";
}
inline std::string Tok(int n) { return Jwt("u" + std::to_string(n), kFuture); }

/// One request in the text form the golden texts use.
inline std::string Describe(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                            const std::string& body) {
    std::string s = method + " " + url + "\n";
    for (const std::string& h : headers) s += h + "\n";
    return s + "\n" + body + "\n---\n";
}

/// The fake server: writes each request down and answers with the next queued answer.
struct Fake {
    std::string log;                                      // every request, Describe()d
    std::vector<okay::http::Result> queue;                // answers in order (the next one is used per request)
    std::function<void(const std::string& url)> before;   // runs before a request is answered (to change session.txt)
    std::function<std::string(const std::string& url)> rewriteUrl;   // how the URL is written down (a clock time out)
    okay::http::Result Answer(const std::string& m, const std::string& u, const std::vector<std::string>& h, const std::string& b) {
        log += Describe(m, rewriteUrl ? rewriteUrl(u) : u, h, b);
        if (before) before(u);
        if (queue.empty()) { okay::http::Result r; r.error = "test: no answer queued"; return r; }
        okay::http::Result r = queue.front();
        queue.erase(queue.begin());
        return r;
    }
};
inline okay::http::Result Http(long status, const std::string& body) {
    okay::http::Result r; r.reached = true; r.status = status; r.body = body; return r;
}
inline okay::http::Result NoAnswer(const std::string& why) { okay::http::Result r; r.error = why; return r; }

/// A signed-in session file, as the launcher writes it.
inline void SaveSession(const std::filesystem::path& dir, const std::string& user, const std::string& token, const std::string& refresh) {
    std::filesystem::create_directories(dir);
    std::ofstream f(dir / "session.txt", std::ios::trunc | std::ios::binary);
    f << user << "\n" << token << "\n" << refresh << "\n";
}

/// Only the request blocks of a golden text, in order.
inline std::string RequestsOf(const std::string& golden) {
    std::string out;
    std::size_t i = 0;
    bool in = false;
    while (i < golden.size()) {
        std::size_t e = golden.find('\n', i);
        if (e == std::string::npos) e = golden.size();
        const std::string line = golden.substr(i, e - i);
        if (!in && (line.rfind("GET ", 0) == 0 || line.rfind("POST ", 0) == 0 || line.rfind("PUT ", 0) == 0 ||
                    line.rfind("DELETE ", 0) == 0 || line.rfind("PATCH ", 0) == 0)) in = true;
        if (in) out += line + "\n";
        if (in && line == "---") in = false;
        i = e + 1;
    }
    return out;
}

} // namespace golden
