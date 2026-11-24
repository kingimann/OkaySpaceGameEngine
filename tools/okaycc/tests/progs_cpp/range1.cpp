// Round 17: auto and range-for over arrays and over a class with begin() and end().
extern "C" int printf(const char *fmt, ...);

struct Item {
    int id;
    float w;
};

template <typename T>
struct List {
    T data[8];
    int n;
    List() : n(0) {}
    void add(T v) { data[n++] = v; }
    T *begin() { return data; }
    T *end() { return data + n; }
};

struct Counted {
    int v;
    Counted() : v(0) {}
    Counted(const Counted &o) : v(o.v + 100) {}
    ~Counted() { printf("~%d ", v); }
};

List<int> makeList() {
    List<int> l;
    l.add(5);
    l.add(6);
    return l;
}

int main() {
    int a[4] = {1, 2, 3, 4};
    int s = 0;
    for (int v : a) s += v;
    for (auto &v : a) v *= 2;
    for (const int &v : a) s += v;
    printf("array %d %d\n", s, a[3]);
    auto x = 3;
    auto y = x * 2.5;
    const auto *p = &a[1];
    auto q = p + 1;
    printf("auto %d %d %d %d\n", x, (int)(y * 2), *p, *q);
    List<Item> items;
    items.add(Item{1, 0.5f});
    items.add(Item{2, 1.5f});
    float tw = 0;
    for (auto &it : items) {
        it.id += 10;
        tw += it.w;
    }
    for (Item it : items) printf("item %d ", it.id);
    printf("w %d\n", (int)(tw * 10));
    int total = 0;
    for (int v : makeList()) {
        if (v == 6) continue;
        total += v;
    }
    printf("rvalue %d\n", total);
    Counted cs[2];
    cs[0].v = 1;
    cs[1].v = 2;
    int found = 0;
    for (Counted c : cs) {
        if (c.v == 101) {
            found = 1;
            break;
        }
    }
    printf("found %d\n", found);
    float fa = 1.5f;
    fa += 2;
    fa *= 2.0f;
    double dd = 1;
    dd -= 0.25;
    int *ip = a;
    ip += 2;
    float sel = s > 3 ? 0.5f : 2;
    double mix = s < 3 ? 1 : fa;
    float lo = 1e30f;
    lo = fa < lo ? fa : lo;
    printf("fp %d %d %d %d %d %d\n", (int)(fa * 10), (int)(dd * 100), *ip, (int)(sel * 10), (int)mix, (int)lo);
    return 0;
}
