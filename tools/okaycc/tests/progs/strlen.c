// exit 5
int len(char *s) {
    int n;
    n = 0;
    while (s[n]) n = n + 1;
    return n;
}
int main() { return len("hello"); }
