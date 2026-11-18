// exit 9
typedef int I;
typedef int *IP;
int main() {
    I x;
    IP p;
    if (sizeof(I) != 4) return 1;
    if (sizeof(IP) != 8) return 2;
    x = 4;
    p = &x;
    *p = 9;
    return x;
}
