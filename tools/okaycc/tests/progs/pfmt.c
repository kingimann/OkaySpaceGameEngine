// exit 0
int printf(char *fmt, ...);
char *strcpy(char *d, char *s);
char *strcat(char *d, char *s);
int atoi(char *s);
int puts(char *s);
int main() {
    char buf[16];
    long v;
    v = 42;
    printf("%5d\n", 42);
    printf("%-4s\n", "hi");
    printf("%08x\n", 255);
    printf("%ld\n", v);
    printf("%u\n", 99);
    strcpy(buf, "ab");
    strcat(buf, "cd");
    if (buf[0] != 'a' || buf[3] != 'd' || buf[4] != 0) return 1;
    if (atoi("  -17") != -17) return 2;
    puts("ok");
    return 0;
}
