#include "codegen.hpp"
#include "diag.hpp"
#include "harness.hpp"

#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int gOk = 0;

static int runExit(const std::string& src, std::string* asmOut) {
    okaycc::Diagnostics d;
    std::string as = okaycc::compileToAssembly(src, d);
    if (asmOut) *asmOut = as;
    if (!d.ok() || as.empty()) {
        std::fprintf(stderr, "no assembly\n%s", d.format().c_str());
        return -1;
    }
    char tmpl[] = "/tmp/okayccXXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) return -1;
    std::string folder(dir);
    std::string asmPath = folder + "/p.s";
    std::string bin = folder + "/p";
    {
        std::ofstream out(asmPath);
        out << as;
    }
    std::string cmd = "gcc -x assembler -o " + bin + " " + asmPath + " >" + folder + "/gcc.txt 2>&1";
    int built = std::system(cmd.c_str());
    if (built != 0) {
        std::ifstream in(folder + "/gcc.txt");
        std::string err((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::fprintf(stderr, "gcc failed\n%s\n%s\n", err.c_str(), as.c_str());
        std::system(("rm -rf " + folder).c_str());
        return -1;
    }
    int run = std::system(bin.c_str());
    std::system(("rm -rf " + folder).c_str());
    if (!WIFEXITED(run)) return -1;
    return WEXITSTATUS(run);
}

static void expectExit(const char* name, const std::string& src, int want) {
    std::string as;
    int got = runExit(src, &as);
    if (got != want) {
        std::fprintf(stderr, "FAIL %s exit %d want %d\n%s\n", name, got, want, as.c_str());
        ++okaycc_test::gFail;
    } else {
        ++gOk;
    }
}

int main() {
    expectExit("forty two", "int main(){return 42;}\n", 42);
    expectExit("add", "int main(){return 40+2;}\n", 42);
    expectExit("call", "int add(int a, int b){return a+b;} int main(){return add(20, 22);}\n", 42);
    expectExit("fact",
               "int fact(int n){int r; r=1; while(n>1){r=r*n; n=n-1;} return r;}\n"
               "int main(){return fact(5);}\n",
               120);
    expectExit("fact rec",
               "int fact(int n){if(n<=1) return 1; return n*fact(n-1);}\n"
               "int main(){return fact(5);}\n",
               120);
    expectExit("if", "int main(){int x; x=1; if(x) return 7; return 3;}\n", 7);
    expectExit("for",
               "int main(){int s; int i; s=0; for(i=1; i<=4; i=i+1) s=s+i; return s;}\n", 10);
    expectExit("seven",
               "int sum(int a,int b,int c,int d,int e,int f,int g){return a+b+c+d+e+f+g;}\n"
               "int main(){return sum(1,2,3,4,5,6,7);}\n",
               28);
    expectExit("div", "int main(){return 20/3;}\n", 6);
    expectExit("mod", "int main(){return 20%3;}\n", 2);
    expectExit("cmp", "int main(){int x; x=3; if(x<4) x=x+10; return x;}\n", 13);
    expectExit("post", "int main(){int x; x=1; x++; return x;}\n", 2);
    expectExit("pointer",
               "int main(){int x; int *p; x=7; p=&x; *p=9; return x;}\n", 9);
    expectExit("array",
               "int main(){int a[3]; a[0]=2; a[1]=3; a[2]=a[0]+a[1]; return a[2];}\n", 5);
    expectExit("global", "int g; int main(){g=4; g=g+3; return g;}\n", 7);
    expectExit("global inc",
               "int g; void inc(){g=g+1;} int main(){g=10; inc(); inc(); return g;}\n", 12);
    expectExit("string", "int main(){char *s; s=\"hi\"; return s[0];}\n", 104);
    expectExit("string 1", "int main(){char *s; s=\"hi\"; return s[1];}\n", 105);
    expectExit("ptr arith",
               "int main(){int a[3]; int *p; a[0]=10; a[1]=20; p=&a[0]; p=p+1; return *p;}\n", 20);
    expectExit("struct",
               "struct P { int x; int y; };\n"
               "int main(){struct P p; p.x=10; p.y=3; return p.x-p.y;}\n",
               7);
    expectExit("arrow",
               "struct P { int x; int y; };\n"
               "int main(){struct P p; struct P *q; p.x=4; p.y=1; q=&p; return q->x+q->y;}\n",
               5);
    expectExit("nested",
               "struct P { int x; int y; };\n"
               "struct Q { struct P p; int z; };\n"
               "int main(){struct Q q; q.p.x=8; q.p.y=1; q.z=2; return q.p.x-q.p.y+q.z;}\n",
               9);
    expectExit("struct global",
               "struct P { int x; }; struct P g; int main(){g.x=6; return g.x;}\n", 6);
    expectExit("struct char",
               "struct C { char a; char b; };\n"
               "int main(){struct C c; c.a=65; c.b=66; return c.a+c.b;}\n",
               131);

    {
        std::string as;
        int got = runExit("int add(int a, int b){return a+b;} int main(){return add(20, 22);}\n", &as);
        bool reg = as.find("%rbx") != std::string::npos || as.find("%r8") != std::string::npos ||
                   as.find("%r9") != std::string::npos || as.find("%r10") != std::string::npos ||
                   as.find("%r11") != std::string::npos || as.find("%r12") != std::string::npos ||
                   as.find("%rsi, %rax") != std::string::npos || as.find("%rdi, %rax") != std::string::npos;
        if (got != 42 || !reg) {
            std::fprintf(stderr, "FAIL regalloc exit %d\n%s\n", got, as.c_str());
            ++okaycc_test::gFail;
        } else {
            ++gOk;
        }
    }
    {
        const char* src =
            "int hot(int n) {\n"
            "  int a; int b; int c; int d; int i;\n"
            "  a = 1; b = 2; c = 3; d = 4; i = 0;\n"
            "  while (i < n) {\n"
            "    a = a + b;\n"
            "    b = b + c;\n"
            "    c = c + d;\n"
            "    d = d + i;\n"
            "    i = i + 1;\n"
            "  }\n"
            "  return a + b + c + d;\n"
            "}\n"
            "int main() { return hot(4); }\n";
        auto stackIn = [](const std::string& as) {
            std::string begin = "hot_while0_cond:";
            std::string end = "hot_while0_end:";
            auto i = as.find(begin);
            auto j = i == std::string::npos ? std::string::npos : as.find(end, i);
            if (i == std::string::npos || j == std::string::npos) return -1;
            int n = 0;
            for (std::size_t p = i; p < j; ) {
                p = as.find("(%rbp)", p);
                if (p == std::string::npos || p >= j) break;
                ++n;
                p += 6;
            }
            return n;
        };
        okaycc::setAllocateRegisters(false);
        okaycc::Diagnostics offD;
        std::string off = okaycc::compileToAssembly(src, offD);
        okaycc::setAllocateRegisters(true);
        okaycc::Diagnostics onD;
        std::string on = okaycc::compileToAssembly(src, onD);
        int offN = stackIn(off);
        int onN = stackIn(on);
        int got = runExit(src, nullptr);
        if (!offD.ok() || !onD.ok() || offN < 0 || onN < 0 || !(onN < offN) || got != 115) {
            std::fprintf(stderr, "FAIL hot loop stack %d -> %d exit %d\n", offN, onN, got);
            ++okaycc_test::gFail;
        } else {
            ++gOk;
        }
    }
    {
        const char* src =
            "namespace N {\n"
            "  int x;\n"
            "  int add(int a) { return a + x; }\n"
            "}\n"
            "int main() { N::x = 3; return N::add(4); }\n";
        std::string as;
        int got = runExit(src, &as);
        if (got != 7 || as.find("_ZN1N3addEi") == std::string::npos || as.find("_ZN1N1xE") == std::string::npos) {
            std::fprintf(stderr, "FAIL namespace exit %d\n%s\n", got, as.c_str());
            ++okaycc_test::gFail;
        } else {
            ++gOk;
        }
    }
    {
        const char* src =
            "int main() {\n"
            "  int x;\n"
            "  x = 1;\n"
            "  int &r = x;\n"
            "  r = 4;\n"
            "  return x;\n"
            "}\n";
        int got = runExit(src, nullptr);
        if (got != 4) {
            std::fprintf(stderr, "FAIL reference exit %d\n", got);
            ++okaycc_test::gFail;
        } else {
            ++gOk;
        }
    }
    {
        const char* src =
            "struct S {\n"
            "  int x;\n"
            "  int get() { return x; }\n"
            "  int add(int a) { return x + a; }\n"
            "};\n"
            "int main() {\n"
            "  struct S s;\n"
            "  s.x = 10;\n"
            "  if (s.get() != 10) return 1;\n"
            "  if (s.add(7) != 17) return 2;\n"
            "  return 0;\n"
            "}\n";
        std::string as;
        int got = runExit(src, &as);
        if (got != 0 || as.find("_ZN1S3getEv") == std::string::npos || as.find("_ZN1S3addEi") == std::string::npos) {
            std::fprintf(stderr, "FAIL method exit %d\n%s\n", got, as.c_str());
            ++okaycc_test::gFail;
        } else {
            ++gOk;
        }
    }
    {
        const char* src =
            "int main() {\n"
            "  bool b;\n"
            "  int *p;\n"
            "  b = 1;\n"
            "  p = nullptr;\n"
            "  if (!b) return 1;\n"
            "  if (p) return 2;\n"
            "  return 0;\n"
            "}\n";
        int got = runExit(src, nullptr);
        if (got != 0) {
            std::fprintf(stderr, "FAIL bool exit %d\n", got);
            ++okaycc_test::gFail;
        } else {
            ++gOk;
        }
    }
    {
        const char* src =
            "char *malloc(int n);\n"
            "void free(char *p);\n"
            "int main() {\n"
            "  int *p;\n"
            "  int v;\n"
            "  p = new int;\n"
            "  *p = 5;\n"
            "  v = *p;\n"
            "  delete p;\n"
            "  return v;\n"
            "}\n";
        int got = runExit(src, nullptr);
        if (got != 5) {
            std::fprintf(stderr, "FAIL new exit %d\n", got);
            ++okaycc_test::gFail;
        } else {
            ++gOk;
        }
    }

    DIR* dir = opendir("tests/progs");
    if (!dir) {
        std::fprintf(stderr, "FAIL cannot open tests/progs\n");
        ++okaycc_test::gFail;
    } else {
        int files = 0;
        while (dirent* ent = readdir(dir)) {
            std::string name = ent->d_name;
            if (name.size() < 3 || name.compare(name.size() - 2, 2, ".c") != 0) continue;
            std::ifstream in("tests/progs/" + name);
            std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            int want = -1;
            if (src.compare(0, 8, "// exit ") == 0) want = std::atoi(src.c_str() + 8);
            expectExit(name.c_str(), src, want);
            ++files;
        }
        closedir(dir);
        if (files == 0) {
            std::fprintf(stderr, "FAIL no programs in tests/progs\n");
            ++okaycc_test::gFail;
        }
    }

    okaycc_test::gPass += gOk;
    std::printf("codegen: %d passed, %d failed\n", okaycc_test::gPass, okaycc_test::gFail);
    return okaycc_test::gFail == 0 ? 0 : 1;
}
