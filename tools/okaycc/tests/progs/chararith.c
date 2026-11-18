// exit 1
int main() {
    unsigned char u;
    signed char c;
    short s;
    u = 200;
    u = u + 100;
    if (u != 44) return 2;
    c = 100;
    c = c + 100;
    if (c != -56) return 3;
    s = 20000;
    s = s + 20000;
    if (s != -25536) return 4;
    return 1;
}
