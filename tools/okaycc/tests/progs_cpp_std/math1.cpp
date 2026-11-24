// Round 18: <cmath> (sqrt exactly rounded; sin, cos, exp, log, log10, pow in lib/m.c), <cstdlib>,
// <queue>, std::string +. The values printed are rounded to 9 digits, so a last-bit difference from
// glibc (which is not always correctly rounded) does not show; sqrt is printed bit for bit.
#include <cmath>
#include <cstdlib>
#include <functional>
#include <queue>
#include <string>
#include <vector>

extern "C" int printf(const char *fmt, ...);

static unsigned long bits(double d) { return *(unsigned long *)&d; }
// 9 digits after the point, as an integer (our printf has no %.9f; this also hides a last-bit
// difference from glibc).
static long fix9(double v) { return (long)std::floor(v * 1e9 + 0.5); }

struct Job {
    int id;
    float cost;
};
struct JobGreater {
    bool operator()(const Job &a, const Job &b) const { return a.cost > b.cost; }
};

int main() {
    unsigned long h = 0;
    for (int i = 1; i < 2000; ++i) h = h * 31 + bits(std::sqrt(i * 0.37));
    printf("sqrt %ld %d %d\n", (long)(h % 1000000007ul), (int)(std::sqrt(2.0f) * 1e6f), (int)std::sqrt(144.0));
    for (int i = -6; i <= 6; ++i) {
        double x = i * 0.7;
        printf("%d sin %ld cos %ld exp %ld\n", i, fix9(std::sin(x)), fix9(std::cos(x)), fix9(std::exp(x) / 1000));
    }
    for (int i = 1; i <= 8; ++i) {
        double x = i * 3.25;
        printf("log %ld log10 %ld pow %ld powf %ld\n", fix9(std::log(x)), fix9(std::log10(x)), fix9(std::pow(x, 1.7) / 1000),
               (long)std::floor((double)std::pow((float)(i / 9.0), 2.4f) * 1e6 + 0.5));
    }
    printf("special %ld %ld %ld %ld\n", fix9(std::pow(-2.0, 3.0)), fix9(std::pow(0.0, 2.0)), fix9(std::sin(0.0)),
           fix9(std::pow(2.0, -2.0)));
    printf("floor %d %d %d %d abs %d %d\n", (int)std::floor(-1.5f), (int)std::ceil(1.25), (int)std::floor(3.0),
           (int)std::ceil(-0.5), std::abs(-7), (int)(std::fabs(-2.5f) * 2));
    std::queue<int> q;
    for (int i = 0; i < 5; ++i) q.push(i * i);
    int qs = 0;
    while (!q.empty()) {
        qs = qs * 10 + q.front() % 10;
        q.pop();
    }
    std::priority_queue<int> pq;
    int vals[6] = {5, 1, 9, 3, 7, 2};
    for (int v : vals) pq.push(v);
    printf("queue %d top", qs);
    while (!pq.empty()) {
        printf(" %d", pq.top());
        pq.pop();
    }
    std::priority_queue<Job, std::vector<Job>, JobGreater> jobs;
    jobs.push({1, 5.0f});
    jobs.push({2, 0.5f});
    jobs.push({3, 2.0f});
    printf(" jobs");
    while (!jobs.empty()) {
        printf(" %d", jobs.top().id);
        jobs.pop();
    }
    std::string s = std::string("Okay") + "Space " + "1.2";
    std::string t = s + std::string("!");
    printf("\nstring %s %d %d\n", t.c_str(), (int)t.size(), t == std::string("OkaySpace 1.2!"));
    void *mem = std::malloc(16);
    std::free(mem);
    printf("atoi %d\n", atoi("123"));
    return 0;
}
