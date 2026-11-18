long syscall(long n, long a, long b, long c);
void exit(int status) { syscall(60, (long)status, 0, 0); }
