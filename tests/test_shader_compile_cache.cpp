// okay::shaderbundle::CompileCache (ShaderBundle.hpp): the Direct3D 11 renderer's compiled shaders kept between runs. The owner's
// okay_editor_perf.log (2026-11-25, Windows, Direct3D 11) shows the first 3D frame at 7-9 s; the desktop build ran D3DCompile over
// the whole renderer text once per entry point at every start. The bound is the number of compiles: every entry the first start,
// none the next, all again when the HLSL text changes, and a damaged file is compiled again instead of being used. A map stands in
// for the folder and a counter for D3DCompile, so this runs on Linux; the real D3DCompile path is unverified here.
#include "test_framework.hpp"
#include "okay/Render/ShaderBundle.hpp"

#include <map>
#include <string>
#include <vector>

using namespace okay::shaderbundle;

namespace {
struct Folder {
    std::map<std::string, std::vector<unsigned char>> files;
    Bundle::ReadFn Reader() {
        return [this](const std::string& n, std::vector<unsigned char>& out) {
            auto it = files.find(n);
            if (it == files.end()) return false;
            out = it->second;
            return true;
        };
    }
    CompileCache::WriteFn Writer() {
        return [this](const std::string& n, const std::vector<unsigned char>& b) { files[n] = b; return true; };
    }
};

int g_compiles = 0;
bool FakeCompile(const char* entry, const char* profile, std::vector<unsigned char>& out) {
    ++g_compiles;
    const std::string s = std::string("DXBC") + entry + "/" + profile;
    out.assign(s.begin(), s.end());
    return true;
}

// One start of the renderer: it asks for every entry point it uses (the table plus two entries the table does not list).
int Start(Folder& f, const char* hlsl, CompileCache& cache) {
    g_compiles = 0;
    cache.Open(f.Reader(), f.Writer(), hlsl);
    for (const EntryReq& q : Entries()) {
        const std::vector<unsigned char>* b = cache.Get(q.entry, q.profile, FakeCompile);
        CHECK(b && std::string(b->begin(), b->end()) == std::string("DXBC") + q.entry + "/" + q.profile);
    }
    CHECK(cache.Get("PSDofCopy", "ps_4_0", FakeCompile) != nullptr);
    CHECK(cache.Get("PSDofBlur", "ps_4_0", FakeCompile) != nullptr);
    CHECK(cache.Get("PSDofCopy", "ps_4_0", FakeCompile) != nullptr);   // asked twice in one start: compiled once
    return g_compiles;
}
}  // namespace

int main() {
    const int listed = (int)Entries().size();
    RUN_SUITE("compiled once, then read back on every later start");
    {
        Folder f;
        { CompileCache c; CHECK(Start(f, "hlsl text v1", c) == listed + 2); CHECK(c.Hits() == 0); }
        CHECK(f.files.count("shaders.manifest") == 1);
        { CompileCache c; CHECK(Start(f, "hlsl text v1", c) == 2); CHECK(c.Hits() == listed); }   // only the two unlisted entries
        { CompileCache c; CHECK(Start(f, "hlsl text v2", c) == listed + 2); }                    // the renderer changed: all again
        { CompileCache c; CHECK(Start(f, "hlsl text v2", c) == 2); }
    }

    RUN_SUITE("a damaged or missing file is compiled again, never used");
    {
        Folder f;
        { CompileCache c; Start(f, "hlsl", c); }
        f.files[FileNameFor("PSFxaa")].back() ^= 0x5a;   // an optional entry: only it is compiled again
        { CompileCache c; CHECK(Start(f, "hlsl", c) == 1 + 2); }
        { CompileCache c; CHECK(Start(f, "hlsl", c) == 2); }   // and stored right again
        f.files.erase(FileNameFor("VSMain"));               // a required entry gone: the folder is not trusted, all compiled
        { CompileCache c; CHECK(Start(f, "hlsl", c) == listed + 2); }
        { CompileCache c; CHECK(Start(f, "hlsl", c) == 2); }
    }

    RUN_SUITE("no folder: compiles as before and keeps nothing; a failed compile is not stored");
    {
        CompileCache c;
        g_compiles = 0;
        c.Open(nullptr, nullptr, "hlsl");
        CHECK(c.Get("VSMain", "vs_4_0", FakeCompile) != nullptr && g_compiles == 1);
        CHECK(c.Get("VSMain", "vs_4_0", FakeCompile) != nullptr && g_compiles == 1);
        Folder f;
        CompileCache d;
        d.Open(f.Reader(), f.Writer(), "hlsl");
        CHECK(d.Get("PSMain", "ps_4_0", [](const char*, const char*, std::vector<unsigned char>&) { return false; }) == nullptr);
        CHECK(f.files.empty());
    }
    TEST_MAIN_RESULT();
}
