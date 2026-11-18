// exit 18
int main() {
    int s;
    int i;
    s = 0;
    for (i = 0; i < 10; i = i + 1) {
        if (i == 3) continue;
        if (i == 7) break;
        s = s + i;
    }
    return s;
}
