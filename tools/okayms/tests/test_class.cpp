// C++ classes: member functions, single inheritance and virtual functions.
// The COFF names follow the MSVC decoration, a class with virtual functions
// has a table in .rdata, and the programs give the same exit code as the same
// source built by MinGW g++ (when it is installed) and run under Wine.

#include "check.hpp"
#include "okayms/coff.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cstdlib>
#include <fstream>
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

bool has_symbol(const okayms::ObjectFile& object, const std::string& name) {
    for (const auto& symbol : object.symbols)
        if (symbol.name == name) return true;
    return false;
}

const okayms::Section* find_section(const okayms::ObjectFile& object, const char* name) {
    for (const auto& section : object.sections)
        if (section.name == name) return &section;
    return nullptr;
}

// Builds `source` with okay-cl's compiler and runs it. Returns the exit code
// or -1. When MinGW g++ exists the same source (plus a main that calls entry)
// is built and run too; the two codes must match and equal `want`.
void run_both(Tally& tally, const std::string& name, const std::string& source, int want,
              okayms::ObjectFile* object_out = nullptr) {
    std::printf("class program %s\n", name.c_str());
    okayms::CompileInput in;
    in.source = source;
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    if (object_out != nullptr) *object_out = compiled.value.object;
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  %s\n", linked.error.c_str());
        return;
    }
    const std::string dir = "/tmp/okayms-class";
    run_shell("mkdir -p " + dir);
    const std::string ok_exe = dir + "/" + name + "-ok.exe";
    CHECK(tally, write_bytes(ok_exe, linked.value));
    const int code = wine_exit(ok_exe);
    CHECK(tally, code == want);
    if (code != want) std::fprintf(stderr, "  okay exit %d, wanted %d\n", code, want);
    if (run_shell("command -v x86_64-w64-mingw32-g++ >/dev/null") != 0) {
        std::printf("skipping g++ comparison: MinGW g++ is not installed\n");
        return;
    }
    const std::string src = dir + "/" + name + ".cpp";
    {
        std::ofstream out(src.c_str());
        out << source << "\nint main() { return entry(); }\n";
    }
    const std::string gpp_exe = dir + "/" + name + "-gpp.exe";
    const int built = run_shell("x86_64-w64-mingw32-g++ -static -fno-exceptions -fno-rtti -o " + gpp_exe + " " + src +
                                " 2>" + dir + "/" + name + ".log");
    CHECK(tally, built == 0);
    if (built != 0) return;
    const int ref = wine_exit(gpp_exe);
    CHECK(tally, ref == code);
    if (ref != code) std::fprintf(stderr, "  g++ exit %d, okay exit %d\n", ref, code);
}

}  // namespace

static void test_methods(Tally& tally) {
    std::printf("test_methods\n");
    okayms::ObjectFile object;
    run_both(tally, "methods",
             "class Counter {\n"
             "public:\n"
             "  int n;\n"
             "  int step;\n"
             "  int add(int k) { n = n + k * step; return n; }\n"
             "  int twice(int k) { add(k); return add(k); }\n"
             "  int get() const { return n; }\n"
             "  int fin(int k);\n"
             "private:\n"
             "  int secret;\n"
             "};\n"
             "int Counter::fin(int k) { return n + k; }\n"
             "struct Pt { int x; int y; int sum() { return x + y; } };\n"
             "int entry(void) {\n"
             "  Counter c;\n"
             "  c.n = 1;\n"
             "  c.step = 2;\n"
             "  c.twice(3);\n"
             "  Counter *p = &c;\n"
             "  p->add(1);\n"
             "  Pt q;\n"
             "  q.x = 4;\n"
             "  q.y = 5;\n"
             "  return c.get() + p->fin(1) + q.sum();\n"
             "}\n",
             40, &object);
    CHECK(tally, has_symbol(object, "?add@Counter@@QEAAHH@Z"));
    CHECK(tally, has_symbol(object, "?get@Counter@@QEBAHXZ"));
    CHECK(tally, has_symbol(object, "?fin@Counter@@QEAAHH@Z"));
    CHECK(tally, has_symbol(object, "?sum@Pt@@QEAAHXZ"));
    // No virtual functions, so no table.
    CHECK(tally, !has_symbol(object, "??_7Counter@@6B@"));
}

static void test_virtual(Tally& tally) {
    std::printf("test_virtual\n");
    okayms::ObjectFile object;
    run_both(tally, "virtual",
             "class Shape {\n"
             "public:\n"
             "  int tag;\n"
             "  virtual int area() { return 1; }\n"
             "  virtual int sides() { return 0; }\n"
             "  int describe() { return area() * 10 + sides(); }\n"
             "};\n"
             "class Rect : public Shape {\n"
             "public:\n"
             "  int w;\n"
             "  int h;\n"
             "  virtual int area() { return w * h; }\n"
             "  int sides() { return 4; }\n"
             "};\n"
             "class Square : public Rect {\n"
             "public:\n"
             "  int area() { return Rect::area() + 1; }\n"
             "};\n"
             "int through_ptr(Shape *s) { return s->describe(); }\n"
             "int entry(void) {\n"
             "  Shape s;\n"
             "  s.tag = 1;\n"
             "  Rect r;\n"
             "  r.w = 3;\n"
             "  r.h = 4;\n"
             "  Square q;\n"
             "  q.w = 2;\n"
             "  q.h = 5;\n"
             "  Shape *p = &r;\n"
             "  Shape &ref = q;\n"
             "  int total = 0;\n"
             "  total += through_ptr(&s);\n"
             "  total += p->area();\n"
             "  total += ref.area();\n"
             "  total += q.describe();\n"
             "  return total;\n"
             "}\n",
             147, &object);
    CHECK(tally, has_symbol(object, "?area@Shape@@UEAAHXZ"));
    CHECK(tally, has_symbol(object, "?sides@Shape@@UEAAHXZ"));
    CHECK(tally, has_symbol(object, "?describe@Shape@@QEAAHXZ"));
    CHECK(tally, has_symbol(object, "?area@Rect@@UEAAHXZ"));
    CHECK(tally, has_symbol(object, "?sides@Rect@@UEAAHXZ"));
    CHECK(tally, has_symbol(object, "?area@Square@@UEAAHXZ"));
    // One table per class, in .rdata, one ADDR64 per virtual function (and one for the type_info slot before it).
    CHECK(tally, has_symbol(object, "??_7Shape@@6B@"));
    CHECK(tally, has_symbol(object, "??_7Rect@@6B@"));
    CHECK(tally, has_symbol(object, "??_7Square@@6B@"));
    const okayms::Section* rdata = find_section(object, ".rdata");
    CHECK(tally, rdata != nullptr);
    if (rdata != nullptr) {
        int addr64 = 0;
        for (const auto& reloc : rdata->relocations)
            if (reloc.type == okayms::kRelAmd64Addr64) ++addr64;
        CHECK(tally, addr64 == 9);  // 6 virtual functions + one type_info address per table (vtable[-1], since round 27)
    }
}

// /O1 and /O2 must give the same answer for the same programs.
static void test_optimized(Tally& tally) {
    std::printf("test_optimized\n");
    for (int level = 1; level <= 2; ++level) {
        okayms::CompileInput in;
        in.optimize = true;
        in.opt_level = level;
        in.source =
            "class A { public: int v; virtual int f() { return v; } int g() { return v + 1; } };\n"
            "class B : public A { public: int f() { return v * 2; } };\n"
            "int entry(void) {\n"
            "  B b;\n"
            "  b.v = 5;\n"
            "  A *p = &b;\n"
            "  return p->f() + b.g();\n"
            "}\n";
        const auto compiled = okayms::compile_c(in);
        CHECK(tally, compiled.ok());
        if (!compiled.ok()) {
            std::fprintf(stderr, "  %s\n", compiled.error.c_str());
            continue;
        }
        const auto linked = okayms::link_executable({compiled.value.object}, {});
        CHECK(tally, linked.ok());
        if (!linked.ok()) continue;
        run_shell("mkdir -p /tmp/okayms-class");
        const std::string exe = "/tmp/okayms-class/opt" + std::to_string(level) + ".exe";
        CHECK(tally, write_bytes(exe, linked.value));
        const int code = wine_exit(exe);
        CHECK(tally, code == 16);
        if (code != 16) std::fprintf(stderr, "  /O%d exit %d, wanted 16\n", level, code);
    }
}

int main() {
    Tally tally;
    test_methods(tally);
    test_virtual(tally);
    test_optimized(tally);
    return tally.finish(3);
}
