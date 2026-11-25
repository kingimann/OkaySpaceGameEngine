// Round 20: assigning a braced list (b = {..}, a[i] = {..}), mutable members, and a class
// declared ahead in a namespace (class P;) used through a pointer before its definition.
extern "C" int printf(const char *fmt, ...);

namespace geo {
namespace detail { class Sink; struct Pts; }

struct Q {
    float x, y, z, w;
};

struct Tab {
    int c;
    const int *dc;
    const int *ac;
};

int feed(detail::Sink &s, int v);
int count(const detail::Pts *p);

namespace detail {
class Sink {
public:
    int total = 0;
    void add(int v) { total += v; }
};
struct Pts {
    int n;
};
}  // namespace detail

int feed(detail::Sink &s, int v) {
    s.add(v);
    return s.total;
}
int count(const detail::Pts *p) { return p ? p->n : -1; }

class Cache {
public:
    int get(int k) const {
        ++hits_;
        last_ = k;
        return k * 2;
    }
    int hits() const { return hits_; }
    int last() const { return last_; }

private:
    mutable int hits_ = 0;
    mutable int last_ = -1;
};

Q flip(Q b) {
    if (b.w < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
    }
    return b;
}
}  // namespace geo

int main() {
    geo::Q q = geo::flip({1.0f, 2.0f, 3.0f, -1.0f});
    printf("%d %d %d %d\n", (int)q.x, (int)q.y, (int)q.z, (int)q.w);
    int dcs[2] = {10, 20}, acs[2] = {30, 40};
    geo::Tab sc[3];
    for (int i = 0; i < 3; ++i) sc[i] = {i, &dcs[i % 2], &acs[(i + 1) % 2]};
    for (int i = 0; i < 3; ++i) printf("%d %d %d\n", sc[i].c, *sc[i].dc, *sc[i].ac);
    geo::detail::Sink s;
    geo::feed(s, 4);
    printf("%d\n", geo::feed(s, 5));
    geo::detail::Pts p;
    p.n = 7;
    printf("%d %d\n", geo::count(&p), geo::count(nullptr));
    const geo::Cache c;
    c.get(3);
    int g = c.get(9);
    printf("%d %d %d\n", g, c.hits(), c.last());
    return 0;
}
