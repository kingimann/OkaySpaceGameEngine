// Round 33: arrays captured by a lambda ([expr.prim.lambda.capture] p10, p15): by reference the
// lambda sees the array itself (sizeof, range-for, writes go to it), by copy it holds a copy made
// when the lambda is made; engine/src/Browser/WebFont.cpp's readBox. sizeof was 8 (a pointer)
// and a copy capture read later writes to the original.
#include <cstdint>
#include <cstdio>
struct Reader {
    int n = 0;
    bool U16(std::uint16_t& u) {
        u = (std::uint16_t)(100 + n++);
        return n < 4;
    }
};
int main() {
    Reader bbox;
    std::int16_t box[4] = {0, 0, 0, 0};
    auto readBox = [&] {
        for (std::int16_t& v : box) {
            std::uint16_t u;
            if (!bbox.U16(u)) return false;
            v = std::int16_t(u);
        }
        return true;
    };
    const bool ok = readBox();
    int sum = 0;
    auto add = [&]() {
        for (auto x : box) sum += x;
        return (int)sizeof(box);
    };
    const int size = add();
    int vals[3] = {1, 2, 3};
    auto copy = [=] {
        int s = 0;
        for (int v : vals) s += v;
        return s * 100 + (int)sizeof(vals) + vals[2];
    };
    auto byRef = [&vals] { return vals[0] + (int)sizeof(vals); };
    vals[2] = 50;
    auto copy2 = copy;
    std::printf("%d %d %d %d %d %d\n", ok, box[0], box[2], box[3], sum, size);
    std::printf("%d %d %d\n", copy(), copy2(), byRef());
    return 0;
}
