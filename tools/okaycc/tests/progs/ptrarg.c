// exit 15
int get(int *p) { return *p; }
int main() {
    int x;
    x = 15;
    return get(&x);
}
