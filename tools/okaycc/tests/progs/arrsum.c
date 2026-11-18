// exit 10
int main() {
    int a[4];
    int i;
    int s;
    a[0] = 1;
    a[1] = 2;
    a[2] = 3;
    a[3] = 4;
    s = 0;
    for (i = 0; i < 4; i = i + 1) s = s + a[i];
    return s;
}
