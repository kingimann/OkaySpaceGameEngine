// exit 0
int printf(char *fmt, ...);
char *in;
int pos;
int peek(void) {
    while (in[pos] == ' ') pos = pos + 1;
    return in[pos];
}
int expr(void);
int factor(void) {
    int v;
    if (peek() == '(') {
        pos = pos + 1;
        v = expr();
        if (peek() == ')') pos = pos + 1;
        return v;
    }
    v = 0;
    while (peek() >= '0' && peek() <= '9') {
        v = v * 10 + (in[pos] - '0');
        pos = pos + 1;
    }
    return v;
}
int term(void) {
    int v;
    v = factor();
    while (peek() == '*' || peek() == '/') {
        int op;
        op = in[pos];
        pos = pos + 1;
        if (op == '*') v = v * factor();
        else v = v / factor();
    }
    return v;
}
int expr(void) {
    int v;
    v = term();
    while (peek() == '+' || peek() == '-') {
        int op;
        op = in[pos];
        pos = pos + 1;
        if (op == '+') v = v + term();
        else v = v - term();
    }
    return v;
}
int eval(char *s) {
    in = s;
    pos = 0;
    return expr();
}
int main() {
    int a;
    int b;
    a = eval("2 + 3 * 4");
    b = eval("(2+3)*4");
    printf("%d %d\n", a, b);
    if (a != 14 || b != 20) return 1;
    return 0;
}
