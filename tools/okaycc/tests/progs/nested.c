// exit 9
struct P { int x; int y; };
struct Q { struct P p; int z; };
int main() {
    struct Q q;
    q.p.x = 8;
    q.p.y = 1;
    q.z = 2;
    return q.p.x - q.p.y + q.z;
}
