// exit 68
union U {
    int x;
    char c;
};
int main() {
    union U u;
    u.x = 0x11223344;
    if (u.c != 0x44) return 1;
    u.c = 7;
    if ((u.x & 0xff) != 7) return 2;
    if (sizeof(union U) != 4) return 3;
    return u.c + 61;
}
