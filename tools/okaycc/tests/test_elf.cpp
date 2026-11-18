#include "elf.hpp"
#include "link.hpp"
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

static std::vector<std::vector<unsigned char>> loadLibc() {
    const char* files[] = {"lib/write.c", "lib/exit.c", "lib/mem.c", "lib/str.c", "lib/printf.c", "lib/extra.c",
                           "lib/alloc.c"};
    std::vector<std::string> inc = {"include"};
    std::vector<std::vector<unsigned char>> objs;
    for (const char* path : files) {
        std::ifstream in(path);
        std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        okaycc::Diagnostics d;
        auto obj = okaycc::compileToObject(src, d, path, inc);
        if (!d.ok() || obj.empty()) return {};
        objs.push_back(std::move(obj));
    }
    return objs;
}

static int runExit(const std::string& src) {
    okaycc::Diagnostics d;
    std::vector<unsigned char> bin = okaycc::compileToElf(src, d);
    if (!d.ok() || bin.empty()) {
        okaycc::Diagnostics d2;
        auto obj = okaycc::compileToObject(src, d2, "t.c");
        auto libc = loadLibc();
        std::string error;
        if (d2.ok() && !obj.empty() && !libc.empty()) {
            libc.insert(libc.begin(), std::move(obj));
            bin = okaycc::linkObjects(libc, error);
            if (!bin.empty()) d.clear();
        }
    }
    if (!d.ok() || bin.empty()) {
        std::fprintf(stderr, "no elf\n%s", d.format().c_str());
        return -1;
    }
    char tmpl[] = "/tmp/okayccXXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) return -1;
    std::string folder(dir);
    std::string path = folder + "/p";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
    }
    if (chmod(path.c_str(), 0755) != 0) {
        std::system(("rm -rf " + folder).c_str());
        return -1;
    }
    int run = std::system(path.c_str());
    std::system(("rm -rf " + folder).c_str());
    if (!WIFEXITED(run)) return -1;
    return WEXITSTATUS(run);
}

static void expectExit(const char* name, const std::string& src, int want) {
    int got = runExit(src);
    if (got != want) {
        std::fprintf(stderr, "FAIL %s exit %d want %d\n", name, got, want);
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
    expectExit("pointer", "int main(){int x; int *p; x=7; p=&x; *p=9; return x;}\n", 9);
    expectExit("array",
               "int main(){int a[3]; a[0]=2; a[1]=3; a[2]=a[0]+a[1]; return a[2];}\n", 5);
    expectExit("global", "int g; int main(){g=4; g=g+3; return g;}\n", 7);
    expectExit("string", "int main(){char *s; s=\"hi\"; return s[0];}\n", 104);
    expectExit("struct",
               "struct P { int x; int y; };\n"
               "int main(){struct P p; p.x=10; p.y=3; return p.x-p.y;}\n",
               7);

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
    std::printf("elf: %d passed, %d failed\n", okaycc_test::gPass, okaycc_test::gFail);
    return okaycc_test::gFail == 0 ? 0 : 1;
}
