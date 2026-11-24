// Round 17: lambdas (no capture, [&], [=], explicit captures), deduced return types, lambdas
// passed to function templates.
extern "C" int printf(const char *fmt, ...);

struct Node {
    int id;
    float x;
};

template <typename It, typename Pred>
It findIf(It first, It last, Pred pred) {
    for (; first != last; ++first)
        if (pred(*first)) return first;
    return last;
}

template <typename T, typename Less>
void sortIns(T *a, int n, Less less) {
    for (int i = 1; i < n; ++i) {
        T v = a[i];
        int j = i - 1;
        while (j >= 0 && less(v, a[j])) {
            a[j + 1] = a[j];
            --j;
        }
        a[j + 1] = v;
    }
}

inline float score(float a, float b) {
    auto gap = [](float p, float q) {
        const float g = p - q > q - p ? p - q : q - p;
        return g > 1.0f ? g : 0.0f;
    };
    return gap(a, b) + gap(b, a) * 0.5f;
}

int main() {
    auto add1 = [](int v) { return v + 1; };
    printf("plain %d %d\n", add1(2), add1(add1(3)));
    int k = 10;
    auto addK = [&](int v) { return v + k; };
    k = 20;
    printf("ref %d\n", addK(1));
    auto copyK = [=](int v) { return v + k; };
    k = 30;
    printf("copy %d\n", copyK(1));
    int hits = 0;
    auto bump = [&hits](int by) { hits += by; };
    bump(2);
    bump(3);
    printf("explicit %d\n", hits);
    Node nodes[4] = {{4, 2.5f}, {1, 0.5f}, {3, 9.0f}, {2, 1.5f}};
    int want = 3;
    Node *hit = findIf(nodes, nodes + 4, [&](const Node &n) { return n.id == want; });
    printf("find %d %d\n", (int)(hit - nodes), (int)hit->x);
    sortIns(nodes, 4, [](const Node &a, const Node &b) { return a.x < b.x; });
    for (const Node &n : nodes) printf("%d ", n.id);
    printf("\n");
    int ids[5] = {5, 3, 9, 1, 7};
    sortIns(ids, 5, [](int a, int b) { return a > b; });
    for (int v : ids) printf("%d ", v);
    printf("score %d\n", (int)(score(5.0f, 1.0f) * 10));
    return 0;
}
