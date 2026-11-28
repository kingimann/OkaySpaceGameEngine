// Shared pieces of the OkayBackend golden request tests (tests/test_okay_backend_golden_auth.cpp, round 2,
// tests/test_okay_backend_golden_db.cpp, round 3, and the later rounds; docs/platform/okay_backend.md section 10.1): a fake server that writes
// down every request in one text form and answers from a queue, made-up tokens, and reading the request blocks back out of a
// golden text. No request ever leaves the process. The address, key and tokens are made up.
#pragma once

#include "test_framework.hpp"
#include "okay/Backend/Transport.hpp"   // RecordingTransport, FromHttp
#include "okay/Backend/Supabase.hpp"    // supabase::Database (DbRecorder)
#include "okay/Platform/Account/AccountDetail.hpp"   // detail::RandomHex
#include "okay/Core/Base64.hpp"
#include "okay/Platform/NativeFetch.hpp"   // okay::http::Result

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace golden {

inline const char* const kUrl = "https://backend.example.test/";   // trailing slash on purpose: every flow must drop it
inline const char* const kKey = "test-publishable-key-0000";
inline const long long kFuture = 4102444800LL;   // 2100-01-01 (long long: `long` is 32 bits on Windows and cut this to 1909)

inline std::string B64Url(const std::string& in) {
    return okay::base64::Encode(reinterpret_cast<const std::uint8_t*>(in.data()), in.size(), okay::base64::Alphabet::Url, false);
}
/// JWT-shaped, unsigned: the client only reads "sub" and "exp".
inline std::string Jwt(const std::string& sub, long long exp) {
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

/// Points an account service's test transport (AccountService::SetTransportForTest) at the fake server `f`, which must
/// outlive it. (A template so this header needs no account service.)
template <class Account>
void HookTo(Account& account, Fake& f) {
    account.SetTransportForTest([&f](const std::string& m, const std::string& u, const std::vector<std::string>& h, const std::string& b) {
        return f.Answer(m, u, h, b);
    });
}
/// A new account service on `dir` with the made-up address and key, sending every request to `f`.
template <class Account>
std::unique_ptr<Account> MakeAccount(const std::filesystem::path& dir, Fake& f) {
    auto a = std::make_unique<Account>(dir, kUrl, kKey);
    HookTo(*a, f);
    return a;
}

/// A signed-in session file, as the launcher writes it.
inline void SaveSession(const std::filesystem::path& dir, const std::string& user, const std::string& token, const std::string& refresh) {
    std::filesystem::create_directories(dir);
    std::ofstream f(dir / "session.txt", std::ios::trunc | std::ios::binary);
    f << user << "\n" << token << "\n" << refresh << "\n";
}
/// A new account service on `dir` signed in as `user` (user id "u1": the subject of Tok(1); refresh token "r1"), sending
/// every request to `f` (rounds 5 and 6).
template <class Account>
std::unique_ptr<Account> SignedInAccount(const std::filesystem::path& dir, Fake& f, const std::string& user) {
    SaveSession(dir, user, Tok(1), "r1");
    return MakeAccount<Account>(dir, f);
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

/// A scenario: a name and a run that returns, as text, the requests it sent and what the program made of the answers.
struct Scenario {
    const char* name;
    std::function<std::string(const std::filesystem::path& dir)> run;
};

/// The golden text called `name`, or null.
template <std::size_t N>
const char* Find(const char* const (&golden)[N][2], const std::string& name) {
    for (const auto& g : golden) if (name == g[0]) return g[1];
    return nullptr;
}

/// Part 1 of a golden test: runs every scenario (each in its own folder under `root`). With `print` it writes the texts in
/// the .inc form to stdout and checks nothing (that is how the texts are recorded); otherwise every text must equal its
/// golden one, and each scenario must have exactly one.
template <std::size_t N>
void RunScenarios(const char* testFile, const std::vector<Scenario>& all, const char* const (&golden)[N][2],
                  const std::filesystem::path& root, bool print) {
    if (print) std::cout << "// Recorded with OKAY_GOLDEN_PRINT=1 (" << testFile << ").\n"
                            "static const char* const kGolden[][2] = {\n";
    std::size_t matched = 0;
    for (const Scenario& s : all) {
        const std::string got = s.run(root / s.name);
        if (print) { std::cout << "    {\"" << s.name << "\", R\"GOLD(" << got << ")GOLD\"},\n"; continue; }
        const char* want = Find(golden, s.name);
        CHECK(want != nullptr);
        if (!want) continue;
        const bool same = got == want;
        if (!same) std::cout << "  MISMATCH in " << s.name << "\n--- golden\n" << want << "--- now\n" << got << "\n";
        CHECK(same);
        matched += same;
    }
    if (print) { std::cout << "};\n"; return; }
    CHECK(matched == all.size());
    CHECK(all.size() == N);
}

/// Part 2's fake server behind a RecordingTransport: a provider called directly sends through `rec`, the fake answers.
struct Recorder {
    okay::backend::RecordingTransport rec;
    Fake fake;
    Recorder() {
        rec.answer = [this](const okay::backend::Request& r) {
            return okay::backend::FromHttp(fake.Answer(r.method, r.url, r.headers, r.bodyFile.empty() ? r.body : std::string()));
        };
    }
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
};

/// Part 2 for Database flows (rounds 5 and 6): the Supabase provider's Database sending straight to the recorder with the
/// made-up address and key, signed in with Tok(1) (user id "u1").
struct DbRecorder : Recorder {
    okay::backend::supabase::Database Db() { return okay::backend::supabase::Database(kUrl, kKey, rec, Tok(1)); }
};

/// Part 2: the requests a provider sent through `r` must be exactly the request blocks of the golden text `want`, and the
/// recorder must have kept one request per block. True when they are; prints both when not.
inline bool SameRequests(const char* name, const char* want, const Recorder& r) {
    const std::string blocks = RequestsOf(want);
    const bool same = r.fake.log == blocks;
    if (!same) std::cout << "  PROVIDER MISMATCH in " << name << "\n--- golden requests\n" << blocks << "--- provider\n" << r.fake.log << "\n";
    CHECK(same);
    std::size_t n = 0;
    for (std::size_t k = r.fake.log.find("\n---\n"); k != std::string::npos; k = r.fake.log.find("\n---\n", k + 1)) ++n;
    CHECK(r.rec.sent.size() == n);
    return same;
}

/// Part 2: runs each named flow on a fresh `Run` (a Recorder with the provider to call) and checks its requests against
/// the golden text of the same name (SameRequests); `also` adds the test's own checks for a flow. Every flow must match.
template <class Run, std::size_t N>
void RunProviderFlows(const std::vector<std::pair<const char*, std::function<void(Run&)>>>& flows,
                      const char* const (&golden)[N][2],
                      const std::function<void(const char* name, const char* want, Run&)>& also = {}) {
    std::size_t same = 0;
    for (const auto& flow : flows) {
        const char* want = Find(golden, flow.first);
        CHECK(want != nullptr);
        if (!want) continue;
        Run p;
        flow.second(p);
        same += SameRequests(flow.first, want, p);
        if (also) also(flow.first, want, p);
    }
    CHECK(same == flows.size());
}

/// A whole golden test after RUN_SUITE: part 1 (RunScenarios) in a fresh folder under the temp folder; then, unless it is
/// printing the texts, `between` (the test's own checks on part 1) and part 2 (RunProviderFlows; every flow must have sent
/// something); the folder is removed. True when it printed (the caller returns 0 then).
template <class Run, std::size_t N>
bool RunGolden(const char* testFile, const char* folder, const std::vector<Scenario>& all,
               const std::vector<std::pair<const char*, std::function<void(Run&)>>>& flows, const char* const (&golden)[N][2],
               const std::function<void()>& between = {}) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / (std::string(folder) + "_" + okay::account::detail::RandomHex(8));
    const bool print = std::getenv("OKAY_GOLDEN_PRINT") != nullptr;
    RunScenarios(testFile, all, golden, root, print);
    if (!print) {
        if (between) between();
        RunProviderFlows<Run>(flows, golden, [](const char*, const char*, Run& p) { CHECK(!p.rec.sent.empty()); });
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    return print;
}

} // namespace golden
