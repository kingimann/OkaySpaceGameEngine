long syscall(long n, long a, long b, long c);
/* write (POSIX): the Linux write system call (1). It returns a long, as <unistd.h> declares it
 * (ssize_t); the C library's own callers declare it returning int, which reads the same low half. */
long write(int fd, char *buf, long n) {
    long r;
    r = syscall(1, (long)fd, (long)buf, n);
    return r;
}
