// exit 7
int kind(int x) {
    int r;
    r = 0;
    switch (x) {
    case 1:
        r = 2;
        break;
    case 2:
    case 3:
        r = 4;
        break;
    default:
        r = 9;
        break;
    }
    return r;
}
int main() {
    int s;
    s = 0;
    s = s + kind(1);
    s = s + kind(3);
    s = s + kind(8);
    if (s != 15) return 1;
    switch (s) {
    case 15:
        return 7;
    default:
        return 0;
    }
}
