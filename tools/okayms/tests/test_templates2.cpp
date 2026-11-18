// Template gaps closed in round 15: explicit and partial specialization, member functions of a
// class template defined outside the class, deduction through a class template parameter
// (length(Node<T> *)) and typename T::type. Each program is also built by MinGW g++ and run under
// Wine; the exit codes must match. Written from ISO/IEC 14882 [temp.spec], [temp.class.spec],
// [temp.mem], [temp.deduct.type] and [temp.res].

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
    const std::string dir = "/tmp/okayms-tmpl2";
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

static void test_specialization(Tally& tally) {
    std::printf("test_specialization\n");
    run_both(tally, "spec",
             "template <typename T> struct Info { int id() const { return 1; } };\n"
             "template <> struct Info<char> { int id() const { return 2; } };\n"
             "template <typename T> struct Info<T *> { int id() const { return 3; } };\n"
             "template <typename T, typename U> struct Two { int id() const { return 10; } };\n"
             "template <typename T> struct Two<T, int> { int id() const { return 20; } };\n"
             "template <> struct Two<char, int> { int id() const { return 30; } };\n"
             "template <typename T> struct Sized { T v; int tag() const { return 4; } };\n"
             "template <typename T> struct Sized<T *> { T *v; int tag() const { return 5; } };\n"
             "template <typename T> int pick(T x) { return 1; }\n"
             "template <> int pick<char>(char x) { return 7; }\n"
             "template <> int pick(int x) { return 9; }\n"
             "int entry(void) {\n"
             "  Info<int> a;\n"
             "  Info<char> b;\n"
             "  Info<int *> c;\n"
             "  Two<short, char> d;\n"
             "  Two<short, int> e;\n"
             "  Two<char, int> f;\n"
             "  Sized<int *> g;\n"
             "  Sized<int> h;\n"
             "  int x = 6;\n"
             "  g.v = &x;\n"
             "  h.v = 2;\n"
             "  char ch = 1;\n"
             "  short sh = 1;\n"
             "  int r = a.id() + b.id() + c.id() + d.id() + e.id() + f.id();\n"
             "  r = r + pick(5) + pick(ch) + pick(sh) + pick<char>(ch);\n"
             "  r = r + g.tag() + h.tag() + *g.v + h.v;\n"
             "  return r % 256;\n"
             "}\n",
             66 + 9 + 7 + 1 + 7 + 5 + 4 + 6 + 2);
}

static void test_outside_members(Tally& tally) {
    std::printf("test_outside_members\n");
    okayms::ObjectFile object;
    run_both(tally, "outside",
             "template <typename T>\n"
             "struct Stack {\n"
             "  T items[8];\n"
             "  int n;\n"
             "  Stack();\n"
             "  Stack(T first);\n"
             "  void push(T v);\n"
             "  T pop();\n"
             "  int size() const;\n"
             "};\n"
             "template <typename T> Stack<T>::Stack() : n(0) { }\n"
             "template <typename T> Stack<T>::Stack(T first) : n(1) { items[0] = first; }\n"
             "template <typename T> void Stack<T>::push(T v) { items[n] = v; n = n + 1; }\n"
             "template <typename U> U Stack<U>::pop() { n = n - 1; return items[n]; }\n"
             "template <typename T> int Stack<T>::size() const { return n; }\n"
             "template <typename T, int N>\n"
             "struct Arr {\n"
             "  T v[N];\n"
             "  int cap() const;\n"
             "  T total(T extra) const;\n"
             "};\n"
             "template <typename T, int N> int Arr<T, N>::cap() const { return N; }\n"
             "template <typename T, int N> T Arr<T, N>::total(T extra) const {\n"
             "  T s = extra;\n"
             "  int i;\n"
             "  for (i = 0; i < N; i = i + 1) s = s + v[i];\n"
             "  return s;\n"
             "}\n"
             "int entry(void) {\n"
             "  Stack<int> s;\n"
             "  s.push(4);\n"
             "  s.push(5);\n"
             "  s.push(6);\n"
             "  int top = s.pop();\n"
             "  Stack<char> c(3);\n"
             "  c.push(2);\n"
             "  Arr<int, 3> a;\n"
             "  a.v[0] = 1; a.v[1] = 2; a.v[2] = 3;\n"
             "  return top * 10 + s.size() + c.size() + c.pop() + a.cap() + a.total(100);\n"
             "}\n",
             60 + 2 + 2 + 2 + 3 + 106, &object);
    CHECK(tally, has_symbol(object, "?push@?$Stack@H@@QEAAXH@Z"));
    CHECK(tally, has_symbol(object, "?size@?$Stack@H@@QEBAHXZ"));
    CHECK(tally, has_symbol(object, "?cap@?$Arr@H$02@@QEBAHXZ"));
}

static void test_deduce_class(Tally& tally) {
    std::printf("test_deduce_class\n");
    run_both(tally, "deduce",
             "template <typename T>\n"
             "struct Node { T value; Node<T> *next; };\n"
             "template <typename T, int N>\n"
             "struct Arr { T v[N]; };\n"
             "template <typename T> int length(Node<T> *n) {\n"
             "  int c = 0;\n"
             "  while (n != 0) { c = c + 1; n = n->next; }\n"
             "  return c;\n"
             "}\n"
             "template <typename T> T head(const Node<T> &n) { return n.value; }\n"
             "template <typename T, int N> int cap(Arr<T, N> &a) { return N; }\n"
             "template <typename T> T second(Node<T> *a) { return a->next->value; }\n"
             "int entry(void) {\n"
             "  Node<int> a;\n"
             "  Node<int> b;\n"
             "  Node<int> c;\n"
             "  a.value = 5; b.value = 6; c.value = 7;\n"
             "  a.next = &b; b.next = &c; c.next = 0;\n"
             "  Node<char> d;\n"
             "  d.value = 2; d.next = 0;\n"
             "  Arr<int, 6> arr;\n"
             "  return length(&a) * 10 + head(a) + second(&a) + length(&d) + head(d) + cap(arr);\n"
             "}\n",
             30 + 5 + 6 + 1 + 2 + 6);
}

static void test_typename(Tally& tally) {
    std::printf("test_typename\n");
    run_both(tally, "typename",
             "template <typename T> struct Wrap { typedef T type; T v; };\n"
             "template <typename T> struct Plain { using value_type = T; T v; value_type get() const { return v; } };\n"
             "template <typename C> typename C::type unwrap(C &c) {\n"
             "  typename C::type tmp = c.v;\n"
             "  return tmp + 1;\n"
             "}\n"
             "template <typename T> struct Holder {\n"
             "  typedef typename Wrap<T>::type inner;\n"
             "  inner x;\n"
             "  int get() const { return x; }\n"
             "};\n"
             "int entry(void) {\n"
             "  Wrap<int> w;\n"
             "  w.v = 10;\n"
             "  Wrap<char> wc;\n"
             "  wc.v = 3;\n"
             "  Wrap<int>::type y = 4;\n"
             "  Holder<int> h;\n"
             "  h.x = 20;\n"
             "  Plain<int> p;\n"
             "  p.v = 8;\n"
             "  Plain<int>::value_type z = p.get();\n"
             "  return unwrap(w) + unwrap(wc) + y + h.get() + z;\n"
             "}\n",
             11 + 4 + 4 + 20 + 8);
}

static void test_traits(Tally& tally) {
    std::printf("test_traits\n");
    run_both(tally, "traits",
             "template <typename T> struct Traits { typedef T type; };\n"
             "template <typename T> struct Traits<T *> { typedef T type; };\n"
             "template <typename P> typename Traits<P>::type deref(P p) { return *p; }\n"
             "template <typename T, int N> struct Tag { int id() const { return 1; } };\n"
             "template <typename T> struct Tag<T, 4> { int id() const { return 2; } };\n"
             "template <int N> struct Tag<int, N> { int id() const { return 3; } };\n"
             "template <typename T> struct Cell { T v; Cell(T x); ~Cell(); int get() const; };\n"
             "template <typename U> Cell<U>::Cell(U x) : v(x) { }\n"
             "template <typename U> Cell<U>::~Cell() { }\n"
             "template <typename V> int Cell<V>::get() const { V copy = v; return copy; }\n"
             "int entry(void) {\n"
             "  int x = 40;\n"
             "  Tag<char, 4> a;\n"
             "  Tag<int, 5> b;\n"
             "  Tag<char, 5> c;\n"
             "  Cell<int> cell(7);\n"
             "  return (deref(&x) + a.id() * 100 + b.id() * 10 + c.id() + cell.get()) % 256;\n"
             "}\n",
             (40 + 200 + 30 + 1 + 7) % 256);
}

int main() {
    Tally tally;
    test_traits(tally);
    test_specialization(tally);
    test_outside_members(tally);
    test_deduce_class(tally);
    test_typename(tally);
    return tally.finish(1);
}
