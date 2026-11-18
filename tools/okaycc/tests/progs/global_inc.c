// exit 12
int g;
void inc() { g = g + 1; }
int main() {
    g = 10;
    inc();
    inc();
    return g;
}
