// sizeof(type) as an argument before a member call (the call after it was not checked).
extern "C" int printf(const char *fmt, ...);
struct R {
    int k = 2;
    int Len(int i) const { return i + k; }
};
int main() {
    R r;
    printf("%d %d %d\n", (int)sizeof(int), r.Len(3), (int)sizeof(R));
    return 0;
}
