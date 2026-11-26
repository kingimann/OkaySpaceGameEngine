// A static table in a member function whose elements name the class's static constants and call a
// constexpr function ([stmt.dcl] p3, [class.static.data] p4), as in Input/Input.hpp.
#include <cstdio>
#include <cstring>
class Keys {
public:
    static constexpr char KeyUp = 1;
    static constexpr char KeyDown = 2;
    static constexpr char KeyF1 = static_cast<char>(100);
    static constexpr char KeyF(int n) { return static_cast<char>(KeyF1 + n - 1); }
    struct Entry {
        const char *name;
        char code;
    };
    static const Entry *Find(const char *s) {
        static const Entry kTable[] = {
            {"up", KeyUp}, {"down", KeyDown}, {"esc", static_cast<char>(27)}, {"f1", KeyF(1)}, {"f12", KeyF(12)},
        };
        for (const Entry &e : kTable)
            if (std::strcmp(e.name, s) == 0) return &e;
        return nullptr;
    }
    static int Sum() {
        static const int kCodes[] = {KeyUp, KeyDown, KeyF(3)};
        int t = 0;
        for (int c : kCodes) t += c;
        return t;
    }
};
int main() {
    const char *names[] = {"up", "down", "esc", "f1", "f12", "none"};
    for (const char *n : names) {
        const Keys::Entry *e = Keys::Find(n);
        std::printf("%s %d\n", n, e ? (int)(unsigned char)e->code : -1);
    }
    std::printf("%d %d\n", Keys::Sum(), Keys::Find("up") == Keys::Find("up") ? 1 : 0);
    int KeyUp = 40;  // a local hides the class's name
    static const int local[] = {KeyUp};
    std::printf("%d\n", local[0]);
    return 0;
}
