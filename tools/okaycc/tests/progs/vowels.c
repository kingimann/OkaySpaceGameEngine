// exit 4
int vowels(char *s) {
    int n;
    int i;
    n = 0;
    i = 0;
    while (s[i]) {
        char c;
        c = s[i];
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') n = n + 1;
        i = i + 1;
    }
    return n;
}
int main() { return vowels("okay code"); }
