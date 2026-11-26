// Round 28: a failed dynamic_cast to a reference throws a std::bad_cast object ([expr.dynamic.cast]
// p9), caught as itself or as std::exception; a cast that works still gives the object.
#include <cstdio>
#include <exception>
#include <typeinfo>

struct Shape {
    virtual ~Shape() {}
    virtual int sides() const { return 0; }
};
struct Square : Shape {
    int sides() const override { return 4; }
};
struct Circle : Shape {};

static int tryCast(Shape& s) {
    try {
        Square& q = dynamic_cast<Square&>(s);
        return q.sides();
    } catch (const std::bad_cast& e) {
        std::printf("bad_cast: %s\n", e.what());
        return -1;
    }
}

int main() {
    Square sq;
    Circle ci;
    std::printf("%d\n", tryCast(sq));
    std::printf("%d\n", tryCast(ci));
    try {
        Shape& s = ci;
        (void)dynamic_cast<Square&>(s);
        std::printf("not reached\n");
    } catch (const std::exception& e) {
        std::printf("exception: %s\n", e.what());
    }
    return 0;
}
