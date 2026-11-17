// exit 6
int a[3] = {1, 2, 3};
struct P {
    int x;
    char c;
};
struct P g = {4, 5};
char *s = "hi";
char buf[] = "ab";
int main() {
    if (a[0] + a[1] + a[2] != 6) return 1;
    if (g.x != 4 || g.c != 5) return 2;
    if (s[0] != 'h' || s[1] != 'i') return 3;
    if (buf[0] != 'a' || buf[1] != 'b' || buf[2] != 0) return 4;
    return a[0] + a[1] + a[2];
}
