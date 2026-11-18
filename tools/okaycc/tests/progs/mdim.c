// exit 7
int main() {
    int a[2][3];
    int i;
    int j;
    i = 0;
    while (i < 2) {
        j = 0;
        while (j < 3) {
            a[i][j] = i * 3 + j;
            j = j + 1;
        }
        i = i + 1;
    }
    return a[1][2] + a[0][2];
}
