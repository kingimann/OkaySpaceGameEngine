// The standalone OkayScript standard library (Stdlib.hpp). Written for this project with the C++ standard library only
// (and the operating system's sleep call: Sleep on Windows, nanosleep elsewhere; no <thread>, CLAUDE.md rule 21a).
#include "okayscript/Stdlib.hpp"
#include "okayscript/CoreBuiltins.hpp"
#include "okayscript/detail/Support.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <istream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

namespace okayscript {

namespace {
std::string Join(Args& a) {
    std::string s;
    for (std::size_t i = 0; i < a.Size(); ++i) {
        if (i) s += ' ';
        s += a.Rt().Text(a[i]);
    }
    return s;
}
namespace fs = std::filesystem;
void SleepSeconds(double s) {
#ifdef _WIN32
    Sleep((DWORD)(s * 1000.0));
#else
    timespec ts;
    ts.tv_sec = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
    while (nanosleep(&ts, &ts) != 0) {}
#endif
}
} // namespace

void InstallCoreLibrary(Interpreter& in) {
    auto& b = in.Rt().builtins;
    InstallMathBuiltins<Value>(b);
    InstallConversionBuiltins<Value>(b);
    InstallTextBuiltins<Value>(b);
    Runtime* rt = &in.Rt();
    InstallCollectionBuiltins<Value>(b, [rt](const Value& fn, std::vector<Value>& args) { return rt->CallValue(fn, args); });
    InstallJsonBuiltins<Value>(b);
    // Our own generator, a new sequence each run (rand_seed(n) repeats one); it lives as long as the functions using it.
    auto rng = std::make_shared<detail::Rng>();
    rng->state = (std::uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
    const RandomSource src = rng->Source();
    InstallRandomBuiltins<Value>(b, RandomSource{
        [rng, src](float lo, float hi) { return src.range(lo, hi); },
        [rng, src](int lo, int hi) { return src.rangeInt(lo, hi); },
        [rng, src](std::uint64_t s) { src.seed(s); }});
    InstallTimeBuiltins<Value>(b);
}

void InstallStandardLibrary(Interpreter& in) {
    InstallCoreLibrary(in);

    Interpreter* self = &in;
    auto printLine = [self](Args& a) {
        std::ostream& o = self->Output();
        o << Join(a) << '\n';
        o.flush();
        return Value{};
    };
    in.Define("print", printLine);
    in.Define("println", printLine);
    in.Define("write", [self](Args& a) {
        std::ostream& o = self->Output();
        o << Join(a);
        o.flush();
        return Value{};
    });
    in.Define("input", [self](Args& a) {
        if (a.Size() > 0) { self->Output() << a[0].AsString(); self->Output().flush(); }
        std::string line;
        if (!std::getline(self->Input(), line)) return Value{std::string()};
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return Value{line};
    }, Arity::Between(0, 1));
    in.Define("exit", [](Args& a) -> Value {
        // A whole number 0..255 (what a process can end with); NaN and fractions are handled like (int).
        int code = a.Size() > 0 ? detail::SafeInt(a[0].AsFloat()) : 0;
        code = code < 0 ? 0 : (code > 255 ? 255 : code);
        throw ExitRequest{code};
    }, Arity::Between(0, 1));
}

void InstallSystemLibrary(Interpreter& in) {
    // sleep(seconds): wait (at most a day at a time).
    in.Define("sleep", [](Args& a) {
        double s = a.Number(0);
        if (!(s > 0)) return Value{};
        SleepSeconds(s > 86400 ? 86400 : s);
        return Value{};
    }, Arity::Exactly(1));
    // ---- Files: the bytes of a file as text. A failure is a runtime error that says why. ----
    in.Define("file_read", [](Args& a) {
        const std::string p = a.String(0);
        std::ifstream f(p, std::ios::binary);
        if (!f) a.Fail("can't read '" + p + "'");
        std::ostringstream ss;
        ss << f.rdbuf();
        std::string s = ss.str();
        detail::CheckTextLength(s.size());
        return Value{s};
    }, Arity::Exactly(1));
    auto writeFile = [](Args& a, bool append) {
        const std::string p = a.String(0);
        std::ofstream f(p, append ? std::ios::binary | std::ios::app : std::ios::binary | std::ios::trunc);
        if (!f) a.Fail("can't write '" + p + "'");
        f << a[1].AsString();
        if (!f) a.Fail("can't write '" + p + "'");
        return Value{true};
    };
    in.Define("file_write", [writeFile](Args& a) { return writeFile(a, false); }, Arity::Exactly(2));
    in.Define("file_append", [writeFile](Args& a) { return writeFile(a, true); }, Arity::Exactly(2));
    in.Define("file_exists", [](Args& a) { std::error_code ec; return Value{fs::exists(a.String(0), ec)}; }, Arity::Exactly(1));
    in.Define("is_dir", [](Args& a) { std::error_code ec; return Value{fs::is_directory(a.String(0), ec)}; }, Arity::Exactly(1));
    in.Define("file_delete", [](Args& a) {   // a file or an empty folder; false when there was nothing
        std::error_code ec;
        const bool removed = fs::remove(a.String(0), ec);
        if (ec) a.Fail("can't delete '" + a.String(0) + "': " + ec.message());
        return Value{removed};
    }, Arity::Exactly(1));
    in.Define("file_size", [](Args& a) {
        std::error_code ec;
        const auto n = fs::file_size(a.String(0), ec);
        if (ec) a.Fail("can't read the size of '" + a.String(0) + "': " + ec.message());
        return Value::Int((std::int64_t)n);
    }, Arity::Exactly(1));
    // list_dir(path): the names in a folder, sorted.
    in.Define("list_dir", [](Args& a) {
        std::error_code ec;
        std::vector<std::string> names;
        for (fs::directory_iterator it(a.String(0), ec), end; !ec && it != end; it.increment(ec))
            names.push_back(it->path().filename().generic_string());
        if (ec) a.Fail("can't list '" + a.String(0) + "': " + ec.message());
        std::sort(names.begin(), names.end());
        Value out = Value::MakeArray();
        for (auto& n : names) out.AsArray()->push_back(Value{n});
        return out;
    }, Arity::Exactly(1));
    in.Define("make_dir", [](Args& a) {   // and the folders above it; true when it was made now
        std::error_code ec;
        const bool made = fs::create_directories(a.String(0), ec);
        if (ec) a.Fail("can't make '" + a.String(0) + "': " + ec.message());
        return Value{made};
    }, Arity::Exactly(1));
    in.Define("delete_dir", [](Args& a) {   // the folder and everything in it: how many entries went
        std::error_code ec;
        const auto n = fs::remove_all(a.String(0), ec);
        if (ec) a.Fail("can't delete '" + a.String(0) + "': " + ec.message());
        return Value::Int((std::int64_t)n);
    }, Arity::Exactly(1));
    // ---- The process ----
    in.Define("env", [](Args& a) {   // env(name [, default])
        const char* v = std::getenv(a.String(0).c_str());
        return v ? Value{std::string(v)} : (a.Size() > 1 ? a[1] : Value{std::string()});
    }, Arity::Between(1, 2));
}

} // namespace okayscript
