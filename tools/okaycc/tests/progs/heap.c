// exit 0
char *malloc(int n);
void free(char *p);
int main() {
    char *p;
    p = malloc(8);
    if (p == 0) return 1;
    p[0] = 'q';
    p[1] = 0;
    if (p[0] != 'q') return 2;
    free(p);
    p = malloc(4);
    if (p == 0) return 3;
    return 0;
}
