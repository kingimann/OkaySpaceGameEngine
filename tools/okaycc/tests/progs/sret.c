// exit 10
struct P {
    int x;
    int y;
};
struct P add(struct P a, struct P b) {
    struct P r;
    r.x = a.x + b.x;
    r.y = a.y + b.y;
    return r;
}
int main() {
    struct P a;
    struct P b;
    struct P c;
    a.x = 1;
    a.y = 2;
    b.x = 3;
    b.y = 4;
    c = add(a, b);
    return c.x + c.y;
}
