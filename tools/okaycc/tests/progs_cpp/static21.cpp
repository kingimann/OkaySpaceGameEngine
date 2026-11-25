// Overloaded static member functions (round 21: the second one was "redefined", Quat::Euler in
// the engine's Quat.hpp).
extern "C" int printf(const char *fmt, ...);
struct V {
    float x, y, z;
};
struct Q {
    float a;
    static Q Make(float x, float y, float z) {
        Q q;
        q.a = x + y * 10 + z * 100;
        return q;
    }
    static Q Make(const V &v) { return Make(v.x, v.y, v.z); }
    static int Count() { return 1; }
    static int Count(int n) { return n * 2; }
    int twice() const { return Count(3) + Count(); }
};
int main() {
    V v{1, 2, 3};
    Q a = Q::Make(4, 5, 6);
    Q b = Q::Make(v);
    printf("%.0f %.0f %d %d\n", a.a, b.a, Q::Count(5), a.twice());
    return 0;
}
