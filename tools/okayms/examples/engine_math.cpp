// Engine headers, compiled by okay-ms with its own <vector>, <string>, <array> and <atomic> (tools/okayms/stdinc/).
// Build and run (the same text also builds with MinGW g++, which is how it is compared):
//   tools/okayms/build/okay-cl tools/okayms/examples/engine_math.cpp /Feengine_math.exe /I engine/include \
//       /I engine/src /I shared /I tools/okayms/stdinc
//   wine engine_math.exe; echo $?      (it must print 0; a number is the first check that failed)
// Under okay-ms there is no C runtime in this program, so it brings its own malloc and square root; g++ uses its own.
#ifdef __GNUC__
#include <cmath>
#include <cstdlib>
#else
char pool_[1 << 16];
unsigned long long used_;
void *malloc(unsigned long long n) {
    char *p = pool_ + used_;
    used_ += ((n + 15) / 16) * 16;
    return p;
}
void free(void *p) {}
namespace std {
// Newton's method from a bit-pattern first guess: enough for single precision.
float sqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float g = x > 1.0f ? x * 0.5f : 1.0f;
    for (int i = 0; i < 30; ++i) g = 0.5f * (g + x / g);
    return g;
}
float fabs(float x) { return x < 0.0f ? -x : x; }
float floor(float x) { float t = (float)(long long)x; return t > x ? t - 1.0f : t; }
float ceil(float x) { float t = (float)(long long)x; return t < x ? t + 1.0f : t; }
float trunc(float x) { return (float)(long long)x; }
float round(float x) { return x < 0.0f ? -floor(-x + 0.5f) : floor(x + 0.5f); }
float fmod(float x, float y) { return x - trunc(x / y) * y; }
float sin(float x) {
    const float pi = 3.14159265f;
    x = fmod(x, 2.0f * pi);
    if (x > pi) x -= 2.0f * pi;
    if (x < -pi) x += 2.0f * pi;
    float term = x, sum = x;
    for (int i = 1; i < 12; ++i) {
        term = -term * x * x / (float)((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}
float cos(float x) { return sin(x + 1.57079633f); }
float tan(float x) { return sin(x) / cos(x); }
float atan(float x) {
    // atan(x) = 2 * atan(x / (1 + sqrt(1 + x * x))) twice, then the series
    for (int k = 0; k < 2; ++k) x = x / (1.0f + sqrt(1.0f + x * x));
    float term = x, sum = x;
    for (int i = 1; i < 10; ++i) {
        term = -term * x * x;
        sum += term / (float)(2 * i + 1);
    }
    return sum * 4.0f;
}
float atan2(float y, float x) {
    const float pi = 3.14159265f;
    if (x > 0.0f) return atan(y / x);
    if (x < 0.0f) return y >= 0.0f ? atan(y / x) + pi : atan(y / x) - pi;
    return y > 0.0f ? pi / 2.0f : y < 0.0f ? -pi / 2.0f : 0.0f;
}
float asin(float x) { return atan2(x, sqrt(1.0f - x * x)); }
float acos(float x) { return atan2(sqrt(1.0f - x * x), x); }
float pow(float x, float y) { return x; }
float exp(float x) { return x; }
float log(float x) { return x; }
float log2(float x) { return x; }
float log10(float x) { return x; }
float hypot(float x, float y) { return sqrt(x * x + y * y); }
}
#endif

#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec3.hpp"
#include <array>
#include <atomic>
#include <string>
#include <vector>

#define CK(n, c) if (!(c)) return n;

typedef okay::Vec3 Vec3;
typedef okay::Mat4 Mat4;

int entry(void) {
    // Vec3 arithmetic and length.
    Vec3 a{3, 4, 0};
    CK(1, a.Magnitude() > 4.999f && a.Magnitude() < 5.001f);
    Vec3 n = a.Normalized();
    CK(2, n.x > 0.599f && n.x < 0.601f && n.y > 0.799f && n.y < 0.801f);
    Vec3 ux{1, 0, 0}, uy{0, 1, 0};
    Vec3 c = Vec3::Cross(ux, uy);
    CK(3, c.x == 0 && c.y == 0 && c.z == 1);
    Vec3 ones{1, 1, 1};
    CK(4, Vec3::Dot(a, ones) == 7.0f);
    // A matrix: identity by its default member initializer, then a product.
    Mat4 id;
    CK(5, id.m[0] == 1 && id.m[5] == 1 && id.m[10] == 1 && id.m[15] == 1 && id.m[1] == 0 && id.m[12] == 0);
    Mat4 t;
    t.m[12] = 5; t.m[13] = 6; t.m[14] = 7;
    Mat4 p = id * t;
    CK(6, p.m[12] == 5 && p.m[13] == 6 && p.m[14] == 7 && p.m[0] == 1);
    // The standard containers written for okay-ms.
    std::vector<Vec3> pts;
    for (int i = 0; i < 10; ++i) pts.push_back(Vec3{(float)i, (float)(i * 2), 1});
    float sum = 0;
    for (const Vec3& v : pts) sum += v.x + v.y;
    CK(7, pts.size() == 10 && sum == 135.0f);
    std::string label = "pts:";
    label += std::to_string((int)pts.size());
    CK(8, label == "pts:10" && label.size() == 6);
    std::array<int, 4> fib = {1, 1, 2, 3};
    int total = 0;
    for (int v : fib) total += v;
    CK(9, total == 7);
    std::atomic<int> counter(0);
    for (int i = 0; i < 5; ++i) counter.fetch_add(i);
    CK(10, counter.load() == 10);
    return 0;
}
#ifdef __GNUC__
int main() { return entry(); }
#endif
