char *memcpy(char *dst, char *src, int n) {
    int i;
    i = 0;
    while (i < n) {
        dst[i] = src[i];
        i = i + 1;
    }
    return dst;
}
char *memset(char *dst, int c, int n) {
    int i;
    i = 0;
    while (i < n) {
        dst[i] = (char)c;
        i = i + 1;
    }
    return dst;
}
