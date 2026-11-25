long syscall(long n, long a, long b, long c);
void exit(int status) { syscall(60, (long)status, 0, 0); }
/* abort (ISO C 7.22.4.1): raise SIGABRT (6) on this process (getpid 39, kill 62), then exit 134 if
 * it was caught or ignored. */
void abort(void) {
    long pid;
    pid = syscall(39, 0, 0, 0);
    syscall(62, pid, 6, 0);
    syscall(60, 134, 0, 0);
}
