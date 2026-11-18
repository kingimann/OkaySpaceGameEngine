// exit 55
int fib(int n) {
    int a;
    int b;
    int i;
    int t;
    a = 0;
    b = 1;
    for (i = 0; i < n; i = i + 1) {
        t = a + b;
        a = b;
        b = t;
    }
    return a;
}
int main() { return fib(10); }
