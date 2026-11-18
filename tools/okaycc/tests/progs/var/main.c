int printf(char *fmt, ...);
int main() {
    printf("n=%d s=%s x=%x c=%c\n", -42, "hi", 255, 'A');
    printf("%d %d %d %d %d %d\n", 1, 2, 3, 4, 5, 6);
    return 0;
}
