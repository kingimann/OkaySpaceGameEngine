int write(int fd, char *buf, int n);
void exit(int status);
char *memcpy(char *dst, char *src, int n);
char *memset(char *dst, int c, int n);
int strlen(char *s);
int strcmp(char *a, char *b);
int main() {
    char a[8];
    char b[8];
    int n;
    memcpy(a, "hi", 3);
    if (a[0] != 'h' || a[1] != 'i' || a[2] != 0) return 1;
    memset(b, 'z', 4);
    b[4] = 0;
    if (strlen(b) != 4) return 2;
    if (strcmp(a, "hi") != 0) return 3;
    if (strcmp("ab", "ac") >= 0) return 4;
    if (strcmp("b", "a") <= 0) return 5;
    n = write(1, "ok\n", 3);
    if (n != 3) return 6;
    exit(0);
    return 7;
}
