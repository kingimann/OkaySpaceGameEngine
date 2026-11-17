// exit 0
struct B {
    int a : 3;
    unsigned b : 5;
    int c : 1;
};
int main() {
    struct B x;
    x.a = 5;
    x.b = 21;
    x.c = 1;
    if (x.a != -3) return 1;
    if (x.b != 21) return 2;
    if (x.c != -1) return 3;
    x.a = 2;
    x.b = 3;
    if (x.a + x.b != 5) return 4;
    if (sizeof(struct B) != 4) return 5;
    return 0;
}
