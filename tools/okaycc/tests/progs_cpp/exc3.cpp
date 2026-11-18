// An exception nobody catches ends the program (abort): no output, killed by SIGABRT. (g++ keeps
// buffered output when it aborts, so this program prints nothing.)
struct E { int v; E(int x) : v(x) {} };
void f() { throw E(1); }
int main() {
    f();
    return 0;
}
