#include "codegen.hpp"
#include "diag.hpp"
#include "harness.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
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

    okaycc_test::gPass += gOk;
    std::printf("codegen: %d passed, %d failed\n", okaycc_test::gPass, okaycc_test::gFail);
    return okaycc_test::gFail == 0 ? 0 : 1;
}
