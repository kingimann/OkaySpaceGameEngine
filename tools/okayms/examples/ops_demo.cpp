// Round 20 demo: operator overloading, overloading by parameter type, f().x, auto, range for and lambdas.
// Build and run (Linux with Wine):
//   tools/okayms/build/okay-cl tools/okayms/examples/ops_demo.cpp /Feops_demo.exe /I tools/okayms/stdinc
//   wine ops_demo.exe; echo $?
// It must print 0. A number is the first check that failed: open this file and count the `if` lines.

struct V {
    int x;
    int y;
    V() { x = 0; y = 0; }
    V(int a, int b) { x = a; y = b; }
    V operator+(const V& o) const { return V(x + o.x, y + o.y); }
    V operator-() const { return V(-x, -y); }
    bool operator==(const V& o) const { return x == o.x && y == o.y; }
    V& operator+=(const V& o) { x += o.x; y += o.y; return *this; }
    int& operator[](int i) { return i == 0 ? x : y; }
    operator int() const { return x * 100 + y; }
};

int size_of_thing(int v) { return 1; }
int size_of_thing(double v) { return 2; }
int size_of_thing(char* s) { return 3; }
int size_of_thing(const V& v) { return 4; }

V make(int a, int b) { return V(a, b); }

int entry(void) {
    V a(1, 2);
    V b(10, 20);
    V c = a + b;
    if (c.x != 11 || c.y != 22) return 1;
    c += a;
    if (c[0] != 12 || c[1] != 24) return 2;
    if (!(-a == V(-1, -2))) return 3;
    int packed = c;
    if (packed != 1224) return 4;
    char text[2];
    if (size_of_thing(3) != 1 || size_of_thing(2.5) != 2 || size_of_thing(text) != 3 || size_of_thing(a) != 4) return 5;
    if (make(7, 8).y != 8) return 6;
    auto d = 2.5;
    auto n = 40 + 2;
    if (d != 2.5 || n != 42) return 7;
    int data[4];
    data[0] = 1; data[1] = 2; data[2] = 3; data[3] = 4;
    int sum = 0;
    for (int v : data) sum += v;
    for (auto& r : data) r = r * 2;
    if (sum != 10 || data[3] != 8) return 8;
    int bonus = 100;
    auto add = [bonus](int v) { return v + bonus; };
    auto twice = [](int v) { return v * 2; };
    if (add(5) != 105 || twice(21) != 42) return 9;
    int total = 0;
    auto count = [&total](int v) { total += v; };
    for (int v : data) count(v);
    if (total != 20) return 10;
    return 0;
}
