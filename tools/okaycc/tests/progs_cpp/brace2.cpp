// Round 18: braced lists as arguments, braced static member definitions, inline variables.
extern "C" int printf(const char *fmt, ...);

struct Tap {
    int index;
    float weight;
};

struct V2 {
    float x = 0, y = 0;
    static const V2 Zero;
    static const V2 One;
};
inline const V2 V2::Zero{0, 0};
inline const V2 V2::One = {1, 1};

struct Box {
    int lo, hi;
    Box(int a, int b) : lo(a), hi(b) {}
};

struct Bag {
    Tap taps[4];
    int n = 0;
    void add(const Tap &t) { taps[n++] = t; }
    void addBox(Box b, int scale) { n += (b.hi - b.lo) * scale; }
};

int weigh(Tap t) { return t.index * 100 + (int)(t.weight * 10); }
float len(const V2 &v) { return v.x + v.y; }

int main() {
    Bag bag;
    bag.add({3, 0.5f});
    bag.add({7, 2.0f});
    bag.add({});
    printf("bag %d %d %d %d %d\n", bag.n, bag.taps[0].index, (int)(bag.taps[1].weight * 10), bag.taps[2].index,
           weigh({4, 1.5f}));
    bag.addBox({2, 5}, 10);
    printf("box %d len %d %d %d\n", bag.n, (int)(len({1.5f, 2.0f}) * 10), (int)V2::One.x, (int)V2::Zero.y);
    return 0;
}
