// A default argument is looked up where the function is declared ([dcl.fct.default] p5): a local
// of the caller named like a member it uses (out, in) is not seen; a base named through a class
// (Outer::Inner) as the second base of a class.
#include <cstdio>
#include <sstream>
#include <string>
struct Outer {
    struct Inner {
        virtual ~Inner() = default;
        virtual int Get() const { return 1; }
        int v = 40;
    };
};
struct Front {
    virtual ~Front() = default;
    int f = 2;
};
struct Both : public Front, public Outer::Inner {
    int Get() const override { return v + f; }
};
static int read(const Outer::Inner &i) { return i.Get(); }
int main() {
    std::ostringstream out;
    std::string in = "5 6";
    std::stringstream ss(in);
    int a = 0, b = 0;
    ss >> a >> b;
    out << a + b;
    Both x;
    std::printf("%s %d\n", out.str().c_str(), read(x));
}
