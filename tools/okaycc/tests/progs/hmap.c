// exit 0
char *malloc(int n);
int printf(char *fmt, ...);
struct Node {
    int key;
    int val;
    struct Node *next;
};
struct Node *tab[8];
int hash(int k) {
    if (k < 0) k = -k;
    return k % 8;
}
void put(int k, int v) {
    int h;
    struct Node *n;
    h = hash(k);
    n = tab[h];
    while (n) {
        if (n->key == k) {
            n->val = v;
            return;
        }
        n = n->next;
    }
    n = (struct Node *)malloc(32);
    n->key = k;
    n->val = v;
    n->next = tab[h];
    tab[h] = n;
}
int get(int k) {
    struct Node *n;
    n = tab[hash(k)];
    while (n) {
        if (n->key == k) return n->val;
        n = n->next;
    }
    return -1;
}
int main() {
    put(1, 10);
    put(9, 20);
    put(3, 30);
    put(1, 11);
    if (get(1) != 11) return 1;
    if (get(9) != 20) return 2;
    if (get(3) != 30) return 3;
    if (get(4) != -1) return 4;
    printf("%d\n", get(1) + get(9) + get(3));
    return 0;
}
