extern "C" int printf(const char *fmt, ...);
struct T {
    int v;
    T() : v(7) { printf("  T()\n"); }
    ~T() { printf("  ~T(%d)\n", v); }
};
struct Q {
    int v;
    Q() : v(3) { printf("  Q()\n"); }
};
int main() {
    int *nums = new int[4];
    nums[0] = 5; nums[3] = 6;
    printf("%d\n", nums[0] + nums[3]);
    delete[] nums;
    T *a = new T[3];
    a[1].v = 9;
    printf("%d %d\n", a[0].v, a[1].v);
    delete[] a;
    Q *q = new Q[2];
    printf("%d\n", q[1].v);
    delete[] q;
    T *none = 0;
    delete[] none;
    printf("end\n");
    return 0;
}
