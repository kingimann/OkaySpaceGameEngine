// exit 0
char *p;
int expr(void);
int peek(void) {
    while (*p == ' ') p = p + 1;
    return *p;
}
int factor(void) {
    int v;
    if (peek() == '(') {
        p = p + 1;
        v = expr();
        if (peek() == ')') p = p + 1;
        return v;
    }
    v = 0;
    while (peek() >= '0' && peek() <= '9') {
        v = v * 10 + (*p - '0');
        p = p + 1;
    }
    return v;
}
int term(void) {
    int v;
    char op;
    v = factor();
    while (peek() == '*' || peek() == '/') {
        op = *p;
        p = p + 1;
        if (op == '*') v = v * factor();
        else v = v / factor();
    }
    return v;
}
int expr(void) {
    int v;
    char op;
    v = term();
    while (peek() == '+' || peek() == '-') {
        op = *p;
        p = p + 1;
        if (op == '+') v = v + term();
        else v = v - term();
    }
    return v;
}
int eval(char *s) {
    p = s;
    return expr();
}
int main() {
    if (eval("2+3*4") != 14) return 1;
    if (eval("(2+3)*4") != 20) return 2;
    if (eval("20/3-1") != 5) return 3;
    return 0;
}
