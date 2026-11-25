// Round 24: std::exception_ptr ([propagation]): current_exception in a handler, the pointer kept
// after the handler ends, copied, rethrown later (twice) and caught by base class; a null one;
// make_exception_ptr. Each thrown object is destroyed once, when the last reference goes.
#include <cstdio>
#include <exception>
#include <stdexcept>
struct Noisy {
    int id;
    Noisy(int i) : id(i) {}
    Noisy(const Noisy &o) : id(o.id) {}
    ~Noisy() { printf("~Noisy %d\n", id); }
};
static std::exception_ptr runJob(int n) {
    try {
        if (n > 1) throw std::runtime_error("job failed");
        if (n == 1) throw Noisy(7);
    } catch (...) {
        return std::current_exception();
    }
    return std::exception_ptr();
}
int main() {
    std::exception_ptr none = runJob(0);
    printf("none %d\n", none ? 1 : 0);
    std::exception_ptr e = runJob(2);
    std::exception_ptr copy = e;
    printf("set %d same %d\n", e ? 1 : 0, e == copy ? 1 : 0);
    for (int i = 0; i < 2; ++i) {
        try {
            std::rethrow_exception(copy);
        } catch (const std::exception &x) {
            printf("caught %s\n", x.what());
        }
    }
    {
        std::exception_ptr n = runJob(1);
        try {
            std::rethrow_exception(n);
        } catch (Noisy &k) {
            printf("noisy %d\n", k.id);
        }
        printf("handler over\n");
    }
    printf("scope over\n");
    std::exception_ptr m = std::make_exception_ptr(std::logic_error("made"));
    try {
        std::rethrow_exception(m);
    } catch (const std::logic_error &x) {
        printf("made: %s\n", x.what());
    }
}
