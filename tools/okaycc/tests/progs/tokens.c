// exit 0
int next(char *s, int *i, char *out) {
    int n;
    while (s[*i] == ' ') *i = *i + 1;
    if (s[*i] == 0) return 0;
    n = 0;
    while (s[*i] != 0 && s[*i] != ' ') {
        out[n] = s[*i];
        n = n + 1;
        *i = *i + 1;
    }
    out[n] = 0;
    return 1;
}
int main() {
    char buf[16];
    int i;
    int c;
    int sum;
    char *s;
    s = "one two three";
    i = 0;
    c = 0;
    sum = 0;
    while (next(s, &i, buf)) {
        c = c + 1;
        sum = sum + buf[0];
    }
    if (c != 3) return 1;
    return sum - (111 + 116 + 116);
}
