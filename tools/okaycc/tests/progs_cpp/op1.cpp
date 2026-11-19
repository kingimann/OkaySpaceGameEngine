// Member operators: arithmetic, comparison, indexing, call, compound assignment, ++ and --, unary.
extern "C" int printf(const char *fmt, ...);

struct V {
    int x;
    int y;
    V(int a, int b) : x(a), y(b) {}
    V operator+(const V &o) const { return V(x + o.x, y + o.y); }
    V operator-(const V &o) const { return V(x - o.x, y - o.y); }
    V operator*(int k) const { return V(x * k, y * k); }
    V operator-() const { return V(-x, -y); }
    bool operator==(const V &o) const { return x == o.x && y == o.y; }
    bool operator!=(const V &o) const { return !(x == o.x && y == o.y); }
    bool operator<(const V &o) const { return x < o.x; }
    int operator[](int i) const { return i ? y : x; }
    int operator()(int k) const { return x * k + y; }
    V &operator+=(const V &o) {
        x += o.x;
        y += o.y;
        return *this;
    }
    V &operator++() {
        x = x + 1;
        y = y + 1;
        return *this;
    }
    int operator++(int) {
        int old = x;
        x = x + 1;
        return old;
    }
};

struct Grid {
    int cells[4];
    Grid() {
        for (int i = 0; i < 4; i = i + 1) cells[i] = i * i;
    }
    int &operator[](int i) { return cells[i]; }
};

int main() {
    V a(1, 2);
    V b(10, 20);
    V c = a + b;
    printf("%d %d\n", c.x, c.y);
    V d = b - a;
    printf("%d %d\n", d.x, d.y);
    V e = a * 5;
    printf("%d %d\n", e.x, e.y);
    V f = -a;
    printf("%d %d\n", f.x, f.y);
    printf("%d %d %d\n", a == a, a == b, a != b);
    printf("%d %d\n", a < b, b < a);
    printf("%d %d\n", a[0], b[1]);
    printf("%d\n", b(3));
    a += b;
    printf("%d %d\n", a.x, a.y);
    ++a;
    printf("%d %d\n", a.x, a.y);
    int old = a++;
    printf("%d %d %d\n", old, a.x, a.y);
    Grid g;
    g[2] = 40;
    g[3] += 5;
    printf("%d %d %d %d\n", g[0], g[1], g[2], g[3]);
    return c.x + g[2];
}
