// exit 1
int main() {
    unsigned int x;
    unsigned char c;
    int s;
    x = 4294967295u;
    x = x + 1;
    if (x != 0) return 2;
    c = 255;
    c = c + 1;
    if (c != 0) return 3;
    s = -1;
    x = 1;
    if (s < x) return 4;
    if (!(x < s)) return 5;
    return 1;
}
