// Function templates: deduced and explicit arguments, several instances, templates calling templates.
extern "C" int printf(const char *fmt, ...);

template <typename T>
T maxOf(T a, T b) {
    if (a > b) return a;
    return b;
}

template <typename T>
void swapValues(T *a, T *b) {
    T tmp = *a;
    *a = *b;
    *b = tmp;
}

template <typename T, int N>
T sumArray(T *p) {
    T total = 0;
    for (int i = 0; i < N; i = i + 1) total = total + p[i];
    return total;
}

template <class T>
T clampTo(T v, T lo, T hi) {
    return maxOf(lo, maxOf<T>(v, lo) > hi ? hi : v);
}

template <typename T>
int sizeOfT() {
    return (int)sizeof(T);
}

struct Point {
    int x;
    int y;
};

int main() {
    int a = 3;
    int b = 9;
    long la = 100;
    long lb = 7;
    double da = 1.5;
    double db = 2.25;
    printf("max %d %ld %f\n", maxOf(a, b), maxOf(la, lb), maxOf(da, db));
    printf("explicit %d\n", maxOf<int>(4, 2));
    swapValues(&a, &b);
    swapValues(&la, &lb);
    printf("swapped %d %d %ld %ld\n", a, b, la, lb);
    int arr[5];
    for (int i = 0; i < 5; i = i + 1) arr[i] = i * i;
    printf("sum %d\n", sumArray<int, 5>(arr));
    printf("sum3 %d\n", sumArray<int, 3>(arr));
    printf("clamp %d %d %d\n", clampTo(5, 1, 10), clampTo(-4, 1, 10), clampTo(50, 1, 10));
    Point p;
    Point q;
    p.x = 1;
    p.y = 2;
    q.x = 8;
    q.y = 9;
    swapValues(&p, &q);
    printf("points %d %d %d %d\n", p.x, p.y, q.x, q.y);
    printf("sizes %d %d %d\n", sizeOfT<char>(), sizeOfT<long>(), sizeOfT<Point>());
    return 0;
}
