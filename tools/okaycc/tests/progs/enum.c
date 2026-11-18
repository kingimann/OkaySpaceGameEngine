// exit 6
enum Color { Red = 1, Green, Blue = 5 };
int main() {
    enum Color c;
    c = Green;
    if (c != 2) return 1;
    if (Red + Blue != 6) return 2;
    return Red + Blue;
}
