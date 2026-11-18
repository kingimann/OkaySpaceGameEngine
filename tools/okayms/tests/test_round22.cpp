// Round 22: the eight engine files that did not compile, braced default arguments and arguments, initializer lists, own <algorithm> additions, <deque>, <chrono>, <cwctype>.
// Programs return 0 when all of their checks hold, or the number of the first check
// that failed. Each is built by okay-cl at /O0, /O1 and /O2 and run under Wine; MinGW g++ builds the same
// text and its exit code must match. Written from ISO/IEC 14882 [dcl.fct.default], [stmt.return],
// [over.oper], [over.match], [over.binary], [over.unary], [over.sub], [over.call], [over.inc], [class.conv.fct],
// [expr.ref], [stmt.return], [dcl.spec.auto], [stmt.ranged], [expr.prim.lambda], and the MSVC name decoration rules
// (operator codes ?H ?G ?D ?K ?A ?R ?8 ?9 ?M ?O ?Y ?Z ?E ?F ?6 ?B).

#include "check.hpp"
#include "okayms/coff.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

int run_shell(const std::string& cmd) {
    const int rc = std::system(cmd.c_str());
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

int wine_exit(const std::string& exe) {
    return run_shell("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + exe + " >/dev/null 2>&1");
}

bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

[[maybe_unused]] bool has_symbol(const okayms::ObjectFile& object, const std::string& name) {
    for (const auto& symbol : object.symbols)
        if (symbol.name == name) return true;
    return false;
}

const char* kChecks = "#define CK(n, c) if (!(c)) return n;\n";

// Builds `source` at each optimization level and runs it; MinGW g++ runs the same text.
void run_both(Tally& tally, const std::string& name, const std::string& source, int want,
              okayms::ObjectFile* object_out = nullptr) {
    std::printf("r22 program %s\n", name.c_str());
    const std::string dir = "/tmp/okayms-r22";
    run_shell("mkdir -p " + dir);
    for (int level = 0; level <= 2; ++level) {
        okayms::CompileInput in;
        in.source = std::string(kChecks) + source;
        in.opt_level = level;
        in.optimize = level > 0;
        in.include_paths.push_back("tools/okayms/stdinc");
        in.include_paths.push_back("stdinc");
        const auto compiled = okayms::compile_c(in);
        CHECK(tally, compiled.ok());
        if (!compiled.ok()) {
            std::fprintf(stderr, "  %s /O%d: %s\n", name.c_str(), level, compiled.error.c_str());
            continue;
        }
        if (object_out != nullptr && level == 0) *object_out = compiled.value.object;
        const auto linked = okayms::link_executable({compiled.value.object}, {});
        CHECK(tally, linked.ok());
        if (!linked.ok()) {
            std::fprintf(stderr, "  %s\n", linked.error.c_str());
            continue;
        }
        const std::string ok_exe = dir + "/" + name + "-ok" + std::to_string(level) + ".exe";
        CHECK(tally, write_bytes(ok_exe, linked.value));
        const int code = wine_exit(ok_exe);
        CHECK(tally, code == want);
        if (code != want) std::fprintf(stderr, "  %s /O%d: okay exit %d, wanted %d\n", name.c_str(), level, code, want);
    }
    if (run_shell("command -v x86_64-w64-mingw32-g++ >/dev/null") != 0) {
        std::printf("skipping g++ comparison: MinGW g++ is not installed\n");
        return;
    }
    const std::string src = dir + "/" + name + ".cpp";
    {
        std::ofstream out(src.c_str());
        out << kChecks << "#include <cstdint>\n#include <cstddef>\n" << source << "\nint main() { return entry(); }\n";
    }
    const std::string gpp_exe = dir + "/" + name + "-gpp.exe";
    const int built = run_shell("x86_64-w64-mingw32-g++ -static -fno-exceptions -fno-rtti -w -o " + gpp_exe + " " + src +
                                " 2>" + dir + "/" + name + ".log");
    CHECK(tally, built == 0);
    if (built != 0) return;
    const int ref = wine_exit(gpp_exe);
    CHECK(tally, ref == want);
    if (ref != want) std::fprintf(stderr, "  g++ exit %d, wanted %d (the test itself is wrong)\n", ref, want);
}

}  // namespace

// A bump allocator for the okay-ms side of a program (no C runtime is linked); g++ uses the real one.
static const char* kAlloc =
    "#ifdef __GNUC__\n"
    "#include <stdlib.h>\n"
    "#else\n"
    "char pool_[262144];\n"
    "unsigned long long used_;\n"
    "void *malloc(unsigned long long n) { char *p = pool_ + used_; used_ += ((n + 15) / 16) * 16; return p; }\n"
    "void free(void *p) { }\n"
    "#endif\n";

static void test_braced_defaults_and_args(Tally& tally) {
    std::printf("test_braced_defaults_and_args\n");
    run_both(tally, "braced_default",
             "struct Opt { int a = 1; int b = 2; };\n"
             "struct V { float x, y; };\n"
             "struct G { int Move(int from, const Opt& o = {}) const { return from * 10 + o.a + o.b; }\n"
             "           int Two(int from, Opt o = {5, 6}) const { return from + o.a + o.b; } };\n"
             "struct C { float r, g; C(float a, float b = 1.0f) : r(a), g(b) {} };\n"
             "static float cd(const C& c = C(3, 4)) { return c.r * 10 + c.g; }\n"
             "static int tk(Opt o) { return o.a * 100 + o.b; }\n"
             "static int tv(int k, V p, V q, const Opt& o, float f) { return k + (int)p.x + (int)p.y * 10 + (int)q.x + o.a + (int)f; }\n"
             "int entry(void) {\n"
             "  G g;\n"
             "  CK(1, g.Move(2) == 23);\n"
             "  CK(2, g.Move(2, Opt{7, 8}) == 35);\n"
             "  CK(3, g.Two(1) == 12);\n"
             "  CK(4, cd() == 34.0f && cd(C(1, 2)) == 12.0f);\n"
             "  CK(5, tk({3, 4}) == 304 && tk(Opt{9, 8}) == 908 && tk({}) == 102 && tk({5}) == 502);\n"
             "  float px = 3, py = 4;\n"
             "  CK(6, tv(1, {px, py}, {}, {}, 0.0f) == 1 + 3 + 40 + 0 + 1);\n"
             "  Opt a{1, 2}; Opt b = {3, 4}; Opt c{}; Opt d{5};\n"
             "  CK(7, a.a == 1 && a.b == 2 && b.a == 3 && b.b == 4 && c.a == 1 && c.b == 2 && d.a == 5 && d.b == 2);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_nested_and_pointer_const(Tally& tally) {
    std::printf("test_nested_and_pointer_const\n");
    run_both(tally, "nested_ret",
             "struct GR { struct Finger { long long id; int x; }; Finger fs[3]; Finger* Find(long long id); const Finger* Find(long long id) const; Finger Get(int i) const; };\n"
             "GR::Finger* GR::Find(long long id) { for (int i = 0; i < 3; ++i) if (fs[i].id == id) return &fs[i]; return 0; }\n"
             "const GR::Finger* GR::Find(long long id) const { for (int i = 0; i < 3; ++i) if (fs[i].id == id) return &fs[i]; return 0; }\n"
             "GR::Finger GR::Get(int i) const { return fs[i]; }\n"
             "typedef unsigned char u8;\n"
             "static int walk(const u8* in, unsigned long n) {\n"
             "  const u8* p = in; const u8* const end = in + n; int s = 0;\n"
             "  auto rd = [&](int& x) { for (;;) { if (p >= end) return false; const u8 b = *p++; x += b; if (b != 255) return true; } };\n"
             "  rd(s); p += 1; p++; return s * 100 + (int)(p - in);\n"
             "}\n"
             "const char* names = \"abcd\";\n"
             "int entry(void) {\n"
             "  GR g; for (int i = 0; i < 3; ++i) { g.fs[i].id = 10 + i; g.fs[i].x = i * 7; }\n"
             "  GR::Finger* f = g.Find(11); CK(1, f != 0 && f->x == 7);\n"
             "  const GR& cg = g; const GR::Finger* cf = cg.Find(12); CK(2, cf != 0 && cf->x == 14 && g.Find(99) == 0);\n"
             "  GR::Finger c = g.Get(2); CK(3, c.id == 12);\n"
             "  u8 buf[5] = {200, 100, 3, 4, 5};\n"
             "  CK(4, walk(buf, 5) == 20003);\n"
             "  const char* q = names; q += 2; q++; CK(5, *q == 'd');\n"
             "  names++; CK(6, *names == 'b');\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_color_default_ctor_arg(Tally& tally) {
    std::printf("test_color_default_ctor_arg\n");
    run_both(tally, "color_default",
             "struct Col { float r, g, b, a;\n"
             "  Col(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}\n"
             "  static Col Parse(int v, bool* ok = nullptr, const Col& fallback = Col(1, 1, 1, 1)) {\n"
             "    int count = 0; while (count < 5) ++count;\n"
             "    if (v < 0) { if (ok) *ok = false; return fallback; }\n"
             "    if (ok) *ok = true; return Col(v, 0, 0, 0.5f); } };\n"
             "struct CS { static bool P(int v, Col& out) { bool ok = false; const Col c = Col::Parse(v, &ok); if (ok) out = c; return ok; } };\n"
             "int entry(void) {\n"
             "  Col o(0, 0, 0); CK(1, CS::P(5, o) && o.r == 5.0f && o.a == 0.5f);\n"
             "  CK(2, !CS::P(-1, o) && o.r == 5.0f);\n"
             "  bool ok = true; Col f = Col::Parse(-3, &ok); CK(3, !ok && f.g == 1.0f && f.a == 1.0f);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_member_defaults_braced(Tally& tally) {
    std::printf("test_member_defaults_braced\n");
    run_both(tally, "trap_braced",
             std::string(kAlloc) + "#include <vector>\n"
             "struct Trap { int group = 0; int restore = 0; };\n"
             "struct VP { float n; VP(float a = 0) : n(a) {} };\n"
             "static Trap mk(int g) { return Trap{g, 2}; }\n"
             "static Trap mk2(int g) { return {g, 3}; }\n"
             "int entry(void) {\n"
             "  std::vector<Trap> v; v.push_back(Trap{1, 2}); v.push_back({3, 4});\n"
             "  CK(1, v.size() == 2 && v[0].group == 1 && v[0].restore == 2 && v[1].group == 3 && v[1].restore == 4);\n"
             "  Trap t = mk(7); Trap u = mk2(8); CK(2, t.group == 7 && t.restore == 2 && u.restore == 3);\n"
             "  Trap w{6}; CK(3, w.group == 6 && w.restore == 0);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_initializer_list(Tally& tally) {
    std::printf("test_initializer_list\n");
    run_both(tally, "init_list",
             std::string(kAlloc) + "#include <vector>\n"
             "#include <initializer_list>\n"
             "static int sum(std::initializer_list<int> l) { int s = 0; for (int v : l) s += v; return s; }\n"
             "struct P { int a, b; };\n"
             "int entry(void) {\n"
             "  std::vector<int> v; v = {1, 2, 3};\n"
             "  CK(1, v.size() == 3 && v[0] == 1 && v[2] == 3);\n"
             "  std::vector<P> w; w = {P{1, 2}, P{3, 4}};\n"
             "  CK(2, w.size() == 2 && w[1].a == 3 && w[1].b == 4);\n"
             "  std::vector<int> z = {4, 5}; CK(3, z.size() == 2 && z[1] == 5);\n"
             "  std::vector<int> y{7, 8, 9}; CK(4, y.size() == 3 && y[2] == 9);\n"
             "  CK(5, sum({1, 2, 3, 4}) == 10);\n"
             "  std::vector<float> f = {1.5f, 2.5f}; CK(6, f[0] + f[1] == 4.0f);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_algorithms(Tally& tally) {
    std::printf("test_algorithms\n");
    run_both(tally, "algorithms",
             std::string(kAlloc) + "#include <vector>\n"
             "#include <algorithm>\n"
             "struct F { long long id; int x; };\n"
             "int entry(void) {\n"
             "  std::vector<F> v; for (int i = 1; i <= 5; ++i) { F f; f.id = i; f.x = i * 2; v.push_back(f); }\n"
             "  long long gone = 3;\n"
             "  v.erase(std::remove_if(v.begin(), v.end(), [&](const F& f) { return f.id == gone; }), v.end());\n"
             "  CK(1, v.size() == 4 && v[2].id == 4);\n"
             "  std::vector<int> w; int d[7] = {5, 3, 9, 1, 7, 3, 8}; for (int i = 0; i < 7; ++i) w.push_back(d[i]);\n"
             "  std::sort(w.begin(), w.end());\n"
             "  CK(2, w[0] == 1 && w[1] == 3 && w[2] == 3 && w[3] == 5 && w[4] == 7 && w[5] == 8 && w[6] == 9);\n"
             "  std::sort(w.begin(), w.end(), [](int a, int b) { return a > b; });\n"
             "  CK(3, w[0] == 9 && w[6] == 1);\n"
             "  std::vector<long long> live; live.push_back(2); live.push_back(4);\n"
             "  CK(4, std::find(live.begin(), live.end(), 4LL) != live.end() && std::find(live.begin(), live.end(), 5LL) == live.end());\n"
             "  CK(5, std::count(w.begin(), w.end(), 3) == 2 && std::count_if(w.begin(), w.end(), [](int a) { return a > 4; }) == 4);\n"
             "  CK(6, std::any_of(w.begin(), w.end(), [](int a) { return a == 8; }) && !std::all_of(w.begin(), w.end(), [](int a) { return a > 1; }));\n"
             "  std::reverse(w.begin(), w.end()); CK(7, w[0] == 1 && w[6] == 9);\n"
             "  CK(8, *std::max_element(w.begin(), w.end()) == 9 && *std::min_element(w.begin(), w.end()) == 1);\n"
             "  std::sort(w.begin(), w.end()); CK(9, *std::lower_bound(w.begin(), w.end(), 5) == 5 && std::upper_bound(w.begin(), w.end(), 3) - w.begin() == 3);\n"
             "  int cnt = 7; CK(10, cnt < 8 && cnt > 3);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_rvalue_references(Tally& tally) {
    std::printf("test_rvalue_references\n");
    run_both(tally, "rvalue_ref",
             std::string(kAlloc) +
             "#include <utility>\n"
             "static int live = 0;\n"
             "struct B { int* p; int n; B(int k) : p(new int(k)), n(1) { ++live; }\n"
             "  B(const B& o) : p(new int(*o.p)), n(0) { ++live; }\n"
             "  B(B&& o) : p(o.p), n(2) { o.p = 0; ++live; }\n"
             "  ~B() { if (p) delete p; --live; }\n"
             "  B& operator=(const B& o) { int* t = new int(*o.p); if (p) delete p; p = t; n = 3; return *this; }\n"
             "  B& operator=(B&& o) { int* t = p; p = o.p; o.p = t; n = 4; return *this; } };\n"
             "static B mk(int k) { B b(k); return b; }\n"
             "static int which(const B&) { return 1; }\n"
             "static int which(B&&) { return 2; }\n"
             "int entry(void) {\n"
             "  {\n"
             "    B a(5); B c(a); B d(std::move(a)); B e(mk(7));\n"
             "    CK(1, c.n == 0 && *c.p == 5 && d.n == 2 && *d.p == 5 && a.p == 0 && e.n != 0 && *e.p == 7);\n"
             "    c = d; CK(2, c.n == 3 && *c.p == 5 && d.p != 0);\n"
             "    B f(9); c = std::move(f); CK(3, c.n == 4 && *c.p == 9 && *f.p == 5);\n"
             "    CK(4, which(c) == 1 && which(std::move(c)) == 2 && which(mk(1)) == 2);\n"
             "  }\n"
             "  CK(5, live == 0);\n"
             "  return 0;\n"
             "}\n",
             0);
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    (void)root;
    Tally tally;
    test_braced_defaults_and_args(tally);
    test_nested_and_pointer_const(tally);
    test_color_default_ctor_arg(tally);
    test_member_defaults_braced(tally);
    test_initializer_list(tally);
    test_algorithms(tally);
    test_rvalue_references(tally);
    return tally.finish(7);
}
