// exit 0
int write(int fd, char *buf, int n);
int main() {
    int n;
    n = write(1, "ok\n", 3);
    if (n != 3) return 1;
    return 0;
}
