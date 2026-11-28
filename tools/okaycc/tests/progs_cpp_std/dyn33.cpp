// Round 33: dynamic initialization of variables of static storage duration whose initializers
// okay-cc does not work out as constants ([basic.start.static] p2, [basic.start.dynamic]): calls
// of constexpr functions and of other functions, reads of other globals, and arrays of them;
// engine/src/Browser/WebFont.cpp's kGlyf = TagOf("glyf") and kKnownTags[63].
#include <cstdint>
#include <cstdio>
#include <string>
namespace {
constexpr std::uint32_t TagOf(const char (&s)[5]) {
    return std::uint32_t(std::uint8_t(s[0])) << 24 | std::uint32_t(std::uint8_t(s[1])) << 16 |
           std::uint32_t(std::uint8_t(s[2])) << 8 | std::uint8_t(s[3]);
}
constexpr std::uint32_t kGlyf = TagOf("glyf"), kLoca = TagOf("loca");
const std::uint32_t kKnown[4] = {TagOf("cmap"), kGlyf, 7, TagOf("OS/2")};
int Count() {
    static int n = 0;
    return ++n;
}
int first = Count(), second = Count();
double half = first / 2.0 + kLoca % 7;
}  // namespace
// A class object built from them: the scalars have their values before its constructor runs.
struct Holder {
    std::string text;
    Holder() : text(std::to_string(kGlyf % 1000) + "/" + std::to_string(second)) {}
};
Holder holder;
static const char* names[] = {"a", "b"};
const char* pick = names[1];
int main() {
    std::printf("%08x %08x %08x %08x %u %08x\n", kGlyf, kLoca, kKnown[0], kKnown[1], kKnown[2], kKnown[3]);
    std::printf("%d %d %.1f %s %s %d\n", first, second, half, holder.text.c_str(), pick, Count());
    return 0;
}
