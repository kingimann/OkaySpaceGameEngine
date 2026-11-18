// exit 0
int main() {
    double d;
    float f;
    int n;
    d = 3.25;
    f = (float)d;
    n = (int)d;
    if (n != 3) return 1;
    if (f != (float)3.25) return 2;
    d = (double)n + 0.5;
    if (d != 3.5) return 3;
    if (!(1.0 < 2.0)) return 4;
    if (2.0 < 1.0) return 5;
    return 0;
}
