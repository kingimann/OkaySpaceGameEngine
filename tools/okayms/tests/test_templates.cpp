// Function templates (explicit, partly explicit and deduced arguments) and class templates (type
// and value parameters, defaults, nested instances such as Box<Box<int> >). A template is kept
// as a token range and parsed again with its parameters bound. Instance names follow the public
// MSVC decoration: ??$twice@H@@YAHH@Z for a function, ?get@?$Box@H@@QEBAHXZ for a member of
// Box<int>, $02 for the value 3. The program is also built by MinGW g++ and run under Wine; the
// exit codes must match. Written from ISO/IEC 14882 [temp] and the public decoration description.

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

// Builds `source` with okay-cl's compiler and runs it. Returns the exit code
// or -1. When MinGW g++ exists the same source (plus a main that calls entry)
// is built and run too; the two codes must match and equal `want`.
void run_both(Tally& tally, const std::string& name, const std::string& source, int want,
              okayms::ObjectFile* object_out = nullptr) {
    std::printf("template program %s\n", name.c_str());
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
    const std::string dir = "/tmp/okayms-tmpl";
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

static void test_templates(Tally& tally) {
    std::printf("test_templates\n");
    okayms::ObjectFile object;
    run_both(tally, "templates",
             "template <typename T>\n"
             "T maxOf(T a, T b) { return a > b ? a : b; }\n"
             "template <typename T, typename U>\n"
             "T convert(U u) { return (T)u; }\n"
             "template <typename T, int N = 3>\n"
             "struct Buf {\n"
             "  T items[N];\n"
             "  int count;\n"
             "  Buf() : count(0) { }\n"
             "  void push(T v) { items[count] = v; count = count + 1; }\n"
             "  T sum() const {\n"
             "    T s = 0;\n"
             "    int i;\n"
             "    for (i = 0; i < count; i = i + 1) s = s + items[i];\n"
             "    return s;\n"
             "  }\n"
             "  int capacity() const { return N; }\n"
             "};\n"
             "template <typename T>\n"
             "struct Box {\n"
             "  T value;\n"
             "  Box(T v) : value(v) { }\n"
             "  T get() const { return value; }\n"
             "};\n"
             "template <typename T, typename U = int>\n"
             "struct Pair {\n"
             "  T first;\n"
             "  U second;\n"
             "  Pair(T a, U b) : first(a), second(b) { }\n"
             "};\n"
             "template <typename T>\n"
             "T twice(T x) { return x + x; }\n"
             "int entry(void) {\n"
             "  int r = maxOf(3, 9) + maxOf<int>(8, 2);\n"
             "  r = r + convert<int>(7);\n"
             "  Buf<int, 4> b;\n"
             "  b.push(1);\n"
             "  b.push(2);\n"
             "  b.push(30);\n"
             "  r = r + b.sum() + b.count + b.capacity();\n"
             "  Box<int> bi(5);\n"
             "  Box<Box<int> > nested(bi);\n"
             "  r = r + bi.get() + nested.get().get();\n"
             "  Pair<int, int> p(1, 2);\n"
             "  Pair<int> q(3, 4);\n"
             "  r = r + p.first + p.second + q.first + q.second;\n"
             "  Buf<char> bc;\n"
             "  bc.push(2);\n"
             "  r = r + twice(twice(5)) + bc.sum() + bc.capacity();\n"
             "  return r % 256;\n"
             "}\n",
             109, &object);
    CHECK(tally, has_symbol(object, "??$twice@H@@YAHH@Z"));
    CHECK(tally, has_symbol(object, "??$maxOf@H@@YAHHH@Z"));
    CHECK(tally, has_symbol(object, "??$convert@HH@@YAHH@Z"));
    CHECK(tally, has_symbol(object, "?get@?$Box@H@@QEBAHXZ"));
    CHECK(tally, has_symbol(object, "??0?$Box@H@@QEAA@H@Z"));
    CHECK(tally, has_symbol(object, "?push@?$Buf@H$03@@QEAAXH@Z"));
    CHECK(tally, has_symbol(object, "?push@?$Buf@D$02@@QEAAXD@Z"));
    CHECK(tally, has_symbol(object, "??0?$Box@U?$Box@H@@@@QEAA@U?$Box@H@@@Z"));
}

int main() {
    Tally tally;
    test_templates(tally);
    return tally.finish(1);
}
