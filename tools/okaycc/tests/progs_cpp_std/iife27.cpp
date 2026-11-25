// A lambda called where it is written ([expr.prim.lambda.closure] p1): with and without captures,
// with arguments, inside a member function and in a static local of a class (Scene/Scene.hpp).
#include <cstdio>
#include <cstdlib>
struct Scene {
    static bool& Flag() { static bool v = [] { const char* e = std::getenv("OKAY_NOPE"); return e && *e == '1'; }(); return v; }
    int k = 4;
    int Twice() const { return [this](int m) { return k * m; }(2); }
};
int main() {
    int base = 10;
    int a = [](int x) { return x * 3; }(3);
    int b = [&] { return base + 1; }();
    const int c = [=](int y, int z) -> int { return base * y + z; }(2, 5);
    Scene s;
    std::printf("%d %d %d %d %d\n", a, b, c, (int)Scene::Flag(), s.Twice());
}
