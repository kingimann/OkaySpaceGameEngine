unsigned long strlen(const char *s) {
    unsigned long n;
    n = 0;
    while (s[n] != 0) n = n + 1;
    return n;
}
int strcmp(char *a, char *b) {
    int i;
    unsigned char ca;
    unsigned char cb;
    i = 0;
    while (1) {
        ca = (unsigned char)a[i];
        cb = (unsigned char)b[i];
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
        i = i + 1;
    }
    return 0;
}
/* ISO C 7.24.4.4: compare at most n characters. */
int strncmp(char *a, char *b, unsigned long n) {
    unsigned long i;
    unsigned char ca;
    unsigned char cb;
    i = 0;
    while (i < n) {
        ca = (unsigned char)a[i];
        cb = (unsigned char)b[i];
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
        i = i + 1;
    }
    return 0;
}
/* ISO C 7.24.5.2: the first (char)c in s; the terminating zero counts. */
char *strchr(char *s, int c) {
    while (1) {
        if (*s == (char)c) return s;
        if (*s == 0) return 0;
        s = s + 1;
    }
    return 0;
}
/* ISO C 7.24.5.5: the last (char)c in s. */
char *strrchr(char *s, int c) {
    char *last;
    last = 0;
    while (1) {
        if (*s == (char)c) last = s;
        if (*s == 0) return last;
        s = s + 1;
    }
    return 0;
}
/* ISO C 7.24.5.7: the first place where all of b appears in a; a itself when b is empty. */
char *strstr(char *a, char *b) {
    unsigned long i;
    if (b[0] == 0) return a;
    while (*a != 0) {
        i = 0;
        while (b[i] != 0 && a[i] == b[i]) i = i + 1;
        if (b[i] == 0) return a;
        a = a + 1;
    }
    return 0;
}
/* ISO C 7.4: the character classes of the "C" locale (ASCII). c is an unsigned char value or EOF. */
int isdigit(int c) { return c >= 48 && c <= 57; }
int isupper(int c) { return c >= 65 && c <= 90; }
int islower(int c) { return c >= 97 && c <= 122; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isxdigit(int c) { return isdigit(c) || (c >= 65 && c <= 70) || (c >= 97 && c <= 102); }
int isspace(int c) { return c == 32 || (c >= 9 && c <= 13); }
int isblank(int c) { return c == 32 || c == 9; }
int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
int isprint(int c) { return c >= 32 && c < 127; }
int isgraph(int c) { return c > 32 && c < 127; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int toupper(int c) {
    if (islower(c)) return c - 32;
    return c;
}
int tolower(int c) {
    if (isupper(c)) return c + 32;
    return c;
}
