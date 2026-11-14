#include "okay/Core/Clipboard.hpp"
#include "okay/Core/Utf8.hpp"

namespace okay {
namespace {

MemoryClipboard& DefaultClipboard() {
    static MemoryClipboard box;
    return box;
}

Clipboard*& Slot() {
    static Clipboard* current = &DefaultClipboard();
    return current;
}

bool IsRealReplacement(const char* start, const char* end) {
    return end - start == 3
        && (unsigned char)start[0] == 0xEF
        && (unsigned char)start[1] == 0xBF
        && (unsigned char)start[2] == 0xBD;
}

} // namespace

std::string SanitizeClipboardText(const std::string& text) {
    std::string out;
    const std::size_t bytes = text.size() < kMaxClipboardBytes ? text.size() : kMaxClipboardBytes;
    out.reserve(bytes < 64 ? bytes : 64);
    const char* p = text.data();
    const char* end = p + bytes;
    std::size_t chars = 0;
    while (p < end && chars < kMaxClipboardChars) {
        const char* start = p;
        const std::uint32_t cp = utf8::Next(p, end);
        if (p == start) break;
        if (cp == utf8::kReplacement && !IsRealReplacement(start, p)) continue;
        if (cp < 32 && cp != static_cast<std::uint32_t>('\t') && cp != static_cast<std::uint32_t>('\n')) continue;
        if (cp == 127 || (cp >= 0x80 && cp <= 0x9F)) continue;
        out.append(start, static_cast<std::size_t>(p - start));
        ++chars;
    }
    return out;
}

Clipboard& GameClipboard() {
    Clipboard* c = Slot();
    return c ? *c : DefaultClipboard();
}

void InstallGameClipboard(Clipboard* clipboard) {
    Slot() = clipboard ? clipboard : &DefaultClipboard();
}

std::string GetClipboardText() {
    return SanitizeClipboardText(GameClipboard().GetText());
}

bool SetClipboardText(const std::string& text) {
    return GameClipboard().SetText(SanitizeClipboardText(text));
}

} // namespace okay
