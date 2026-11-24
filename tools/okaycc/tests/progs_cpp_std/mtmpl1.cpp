// Round 18: member function templates (deduced from the call), vector::emplace_back, the range
// constructor, vector<vector<T>>.
#include <cstdint>
#include <vector>

extern "C" int printf(const char *fmt, ...);

struct Acc {
    long total = 0;
    template <typename T>
    void add(T v) { total += (long)(v * 2); }
    template <typename T, typename U>
    long mix(T a, U b) const { return (long)a * 100 + (long)b; }
};

template <typename T>
struct Box {
    T held;
    template <typename U>
    T scaled(U k) const { return (T)(held * k); }
};

struct Pt {
    int x, y;
    Pt(int a, int b) : x(a), y(b) {}
};

int main() {
    Acc a;
    a.add(3);
    a.add(2.5);
    a.add((short)4);
    printf("acc %ld %ld\n", a.total, a.mix(7, 3.9));
    Box<int> b;
    b.held = 6;
    printf("box %d %d\n", b.scaled(2), b.scaled(1.5));
    const std::uint8_t raw[6] = {1, 2, 3, 4, 5, 6};
    std::vector<std::vector<std::uint8_t>> rows;
    rows.emplace_back(raw, raw + 3);
    rows.emplace_back(raw + 3, raw + 6);
    std::vector<Pt> pts;
    pts.emplace_back(1, 2);
    pts.emplace_back(3, 4);
    std::vector<std::uint8_t> copy(raw, raw + 0);
    printf("rows %d %d %d %d pts %d %d empty %d\n", (int)rows.size(), (int)rows[0].size(), rows[1][2], rows[0][0],
           pts[1].x, pts[0].y + pts[1].y, (int)copy.size());
    return 0;
}
