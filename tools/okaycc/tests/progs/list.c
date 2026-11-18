// exit 4
struct Node {
    int v;
    struct Node *n;
};
int sum(struct Node *p) {
    int s;
    s = 0;
    while (p) {
        s = s + p->v;
        p = p->n;
    }
    return s;
}
int main() {
    struct Node a;
    struct Node b;
    a.v = 1;
    b.v = 3;
    a.n = &b;
    b.n = 0;
    return sum(&a);
}
