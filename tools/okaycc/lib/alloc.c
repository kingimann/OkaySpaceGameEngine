long syscall(long n, long a, long b, long c);
char *brk_cur;
int brk_on;
char *malloc(int n) {
    long cur;
    long next;
    char *p;
    if (n < 1) n = 1;
    if (n % 8) n = n + (8 - (n % 8));
    if (!brk_on) {
        cur = syscall(12, 0, 0, 0);
        brk_cur = (char *)cur;
        brk_on = 1;
    }
    next = (long)brk_cur + n;
    cur = syscall(12, next, 0, 0);
    if (cur < next) return 0;
    p = brk_cur;
    brk_cur = (char *)next;
    return p;
}
void free(char *p) { p = p; }
