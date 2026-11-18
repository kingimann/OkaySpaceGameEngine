long syscall(long n, long a, long b, long c);
int write(int fd, char *buf, int n) {
    long r;
    r = syscall(1, (long)fd, (long)buf, (long)n);
    return (int)r;
}
