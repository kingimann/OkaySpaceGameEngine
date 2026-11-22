// exit 9
int main() {
    int x;
    x = 1;
    if (x && 0) return 1;
    if (x || 0) return 9;
    return 2;
}
