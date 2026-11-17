// exit 0
struct Slot {
    int key;
    int val;
    int used;
};
struct Slot tab[16];
int hash(int k) {
    if (k < 0) k = -k;
    return k % 16;
}
void put(int k, int v) {
    int i;
    int n;
    i = hash(k);
    n = 0;
    while (n < 16) {
        if (tab[i].used == 0 || tab[i].key == k) {
            tab[i].used = 1;
            tab[i].key = k;
            tab[i].val = v;
            return;
        }
        i = (i + 1) % 16;
        n = n + 1;
    }
}
int get(int k) {
    int i;
    int n;
    i = hash(k);
    n = 0;
    while (n < 16) {
        if (tab[i].used == 0) return -1;
        if (tab[i].key == k) return tab[i].val;
        i = (i + 1) % 16;
        n = n + 1;
    }
    return -1;
}
int main() {
    put(1, 10);
    put(17, 20);
    put(3, 30);
    if (get(1) != 10) return 1;
    if (get(17) != 20) return 2;
    if (get(3) != 30) return 3;
    if (get(4) != -1) return 4;
    put(1, 11);
    if (get(1) != 11) return 5;
    return 0;
}
