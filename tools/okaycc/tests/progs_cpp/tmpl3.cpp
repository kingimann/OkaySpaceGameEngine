extern "C" int printf(const char *fmt, ...);

template <typename T>
T maxOf(T a, T b) { return a > b ? a : b; }

template <typename T, typename U>
T convert(U u) { return (T)u; }

template <typename T, int N>
struct Buf {
    T items[N];
    int count;
    Buf() : count(0) {}
    void push(T v) { items[count] = v; count = count + 1; }
    T sum() const {
        T s = 0;
        for (int i = 0; i < count; i = i + 1) s = s + items[i];
        return s;
    }
    int capacity() const { return N; }
};

template <typename T>
struct Box {
    T value;
    Box(T v) : value(v) {}
    T get() const { return value; }
};

template <typename T, typename U = int>
struct Pair {
    T first;
    U second;
    Pair(T a, U b) : first(a), second(b) {}
};

template <typename T>
T twice(T x) { return x + x; }

int main() {
    printf("%d %d\n", maxOf(3, 9), maxOf<int>(8, 2));
    printf("%d\n", convert<int>(7));
    Buf<int, 4> b;
    b.push(1);
    b.push(2);
    b.push(30);
    printf("%d %d %d\n", b.sum(), b.count, b.capacity());
    Box<int> bi(5);
    Box<Box<int> > nested(bi);
    printf("%d %d\n", bi.get(), nested.get().get());
    Pair<int, int> p(1, 2);
    Pair<int> q(3, 4);
    printf("%d %d %d %d\n", p.first, p.second, q.first, q.second);
    printf("%d\n", twice(twice(5)));
    return 0;
}
