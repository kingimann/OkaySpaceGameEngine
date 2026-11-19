// Arrays of class objects: each element is constructed in order and destroyed in reverse order.
extern "C" int printf(const char *fmt, ...);

struct Cell {
    int v;
    Cell() : v(7) { printf("  Cell()\n"); }
    ~Cell() { printf("  ~Cell(%d)\n", v); }
};

struct Row {
    Cell left;
    int tag;
    Row() : tag(3) { printf("  Row()\n"); }
    ~Row() { printf("  ~Row(%d)\n", tag); }
};

int main() {
    printf("start\n");
    {
        Cell a[3];
        a[1].v = 9;
        printf("sum %d\n", a[0].v + a[1].v + a[2].v);
    }
    printf("middle\n");
    Row rows[2];
    rows[1].tag = 5;
    for (int i = 0; i < 2; i = i + 1) {
        Cell extra[2];
        if (i == 0) continue;
        printf("i=%d\n", i);
    }
    printf("end\n");
    return 0;
}
