// exit 0
/* Round 20: a local char array set from a string literal (ISO C 6.7.9p14) was left
   uninitialized; a static one was zero. */
int printf(const char *fmt, ...);

int show(void) {
    static char st[8] = "stat";
    st[0] = st[0] + 1;
    return printf("%s\n", st);
}

int main(void) {
    char buf[16] = "abcdefgh";
    char exact[3] = "xyz";
    char unsized[] = "hello, world";
    char small[2] = "";
    int i;
    printf("[%s] %d %d %d\n", buf, buf[0], buf[8], buf[15]);
    printf("%c%c%c %d\n", exact[0], exact[1], exact[2], (int)sizeof(exact));
    printf("[%s] %d\n", unsized, (int)sizeof(unsized));
    printf("%d %d\n", small[0], small[1]);
    for (i = 0; i < 3; ++i) {
        char line[6] = "ab";
        line[2] = (char)('0' + i);
        printf("%s\n", line);
    }
    show();
    show();
    return 0;
}
