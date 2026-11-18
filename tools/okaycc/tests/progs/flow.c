// exit 0
static int bump(void) {
    static int n = 1;
    n += 1;
    return n;
}
int main() {
    const int k = 3;
    volatile int v;
    int i;
    v = 1;
    i = 0;
start:
    if (i < k) {
        i++;
        goto start;
    }
    if (i != 3) return 1;
    if (bump() != 2) return 2;
    if (bump() != 3) return 3;
    if (v != 1) return 4;
    return 0;
}
