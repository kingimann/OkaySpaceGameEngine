// exit 7
void swap(int *a, int *b) {
    int t;
    t = *a;
    *a = *b;
    *b = t;
}
int main() {
    int x;
    int y;
    x = 3;
    y = 7;
    swap(&x, &y);
    return x;
}
