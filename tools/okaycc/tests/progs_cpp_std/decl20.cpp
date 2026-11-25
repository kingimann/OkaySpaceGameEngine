// Round 20: declarations in if/while conditions, local names that are also std function
// templates, std::min<T> with explicit template arguments, and range-for over a braced list.
#include <algorithm>
#include <cstdint>
#include <vector>

extern "C" int printf(const char *fmt, ...);

struct Finger {
    long long id;
    int moved;
};

struct Pad {
    std::vector<Finger> fingers;
    Finger *Find(long long id) {
        for (Finger &f : fingers)
            if (f.id == id) return &f;
        return nullptr;
    }
    int Touch(long long id) {
        if (Finger *f = Find(id)) {
            f->moved += 1;
            return f->moved;
        } else {
            return -1;
        }
    }
};

static int countWords(const char *s) {
    int count = 0;
    std::size_t pos = 0;
    while (s[pos] && count < 5) {
        if (s[pos] == ' ') ++count;
        ++pos;
    }
    int min = count < 3 ? count : 3;
    return count * 10 + min;
}

int main() {
    Pad p;
    p.fingers.push_back({7, 0});
    p.fingers.push_back({9, 2});
    int a = p.Touch(7), b = p.Touch(9), c = p.Touch(8);
    printf("%d %d %d\n", a, b, c);
    if (int n = (int)p.fingers.size()) printf("n %d\n", n);
    if (const Finger *f = p.Find(100)) printf("found %lld\n", f->id);
    else printf("none\n");
    printf("%d %d\n", countWords("a b c d e f g h"), countWords("x y"));
    std::uint32_t sz = 100;
    std::uint64_t size = 70, body = 20;
    const std::uint64_t avail = std::min<std::uint64_t>(sz, size - body);
    int lo = std::max<int>(3, (int)size - 90);
    printf("%d %d\n", (int)avail, lo);
    float xa = 1.5f, xb = -2.0f;
    float sum = 0;
    for (float xc : {xa, xb, 4.0f}) sum += xc * 2;
    int k = 0;
    for (int v : {3, 1, 4, 1, 5}) k = k * 10 + v;
    printf("%d %d\n", (int)sum, k);
    return 0;
}
