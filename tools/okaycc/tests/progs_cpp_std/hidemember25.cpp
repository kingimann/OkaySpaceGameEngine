// A member declared in a class hides the members of that name in its bases ([class.member.lookup]):
// a derived erase template is called, not the base's erase. And a pointer to a derived class
// prefers a base pointer parameter to void * ([over.ics.rank] p4.4).
#include <cstdio>
struct B {
    int erase(int a, int b) { return a + b; }
};
struct T : B {
    int touched = 0;
    template <class X, class Y>
    int erase(X a, Y b) {
        ++touched;
        return B::erase(a, b) * 10;
    }
};
struct Buf {
    virtual ~Buf() {}
};
struct FileBuf : Buf {};
static const char *take(const void *) { return "void"; }
static const char *take(Buf *) { return "buf"; }
int main() {
    T t;
    int r = t.erase(1, 2);
    FileBuf fb;
    std::printf("%d %d %s %s\n", r, t.touched, take(&fb), take((const void *)&fb));
}
