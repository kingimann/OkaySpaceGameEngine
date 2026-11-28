// Round 33: a constructor or destructor defined outside its class sees the class's enumerators
// and static constants ([basic.lookup.unqual] p8, [class.mem]); engine/src/Debug/MemoryTracker.cpp
// (kBuiltinCount); a static member function too (engine/src/Input/Input.cpp, PadButtonCount).
#include <cstdio>
struct Tracker {
    enum Cat { kA, kB, kBuiltinCount };
    static constexpr int kAuto = -1;
    const char* slots[kBuiltinCount];
    int workers;
    explicit Tracker(int w = kAuto);
    ~Tracker();
    static const char* Label(int b);
};
const char* Tracker::Label(int b) {
    if (b < 0 || b >= kBuiltinCount) return "?";
    static const char* const kL[kBuiltinCount] = {"L0", "L1"};
    return kL[b];
}
Tracker::Tracker(int w) : workers(w == kAuto ? kBuiltinCount * 4 : w) {
    static const char* const kNames[kBuiltinCount] = {"a", "b"};
    for (int i = 0; i < kBuiltinCount; ++i) slots[i] = kNames[i];
}
Tracker::~Tracker() { std::printf("bye %d\n", kBuiltinCount + kAuto); }
int main() {
    Tracker t, u(3);
    std::printf("%s %s %d %d %s %s\n", t.slots[0], t.slots[1], t.workers, u.workers, Tracker::Label(1), Tracker::Label(5));
    return 0;
}
