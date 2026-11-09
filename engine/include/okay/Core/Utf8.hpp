#pragma once
// UTF-8 helpers for text players read in any language: walking code points (forwards and
// backwards), counting them, cutting a string on a code point boundary, checking and
// repairing it, and converting to and from UTF-16 / UTF-32 / the platform's wide string.
// Malformed bytes read as U+FFFD and always advance at least one byte, so a loop over any
// string ends. Case folding and normalisation are not here.
#include <cstddef>
#include <cstdint>
#include <string>

namespace okay::utf8 {

constexpr std::uint32_t kReplacement = 0xFFFD;

/// The code point at `p`, advancing `p` past it (never past `end`). Overlong forms,
/// surrogates (U+D800..DFFF), code points above U+10FFFF and bytes that can never
/// start a sequence (C0, C1, F5..FF) give U+FFFD. A truncated or broken sequence
/// consumes its valid leading bytes only (the "maximal subpart"), so the byte that
/// broke it is read again as the start of the next code point.
///
/// At or past `end` there is nothing to read: it returns 0 and leaves `p` at `end`
/// (it used to read one byte beyond the string, and the string overload walked its
/// index past the end).
inline std::uint32_t Next(const char*& p, const char* end) {
    if (p >= end) { p = end; return 0; }
    const unsigned char c = (unsigned char)*p++;
    if (c < 0x80) return c;
    if (c < 0xC2 || c > 0xF4) return kReplacement;     // continuation byte alone, C0/C1 overlong, F5..FF
    const int extra = c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
    std::uint32_t cp = c & (0x3Fu >> extra);
    for (int k = 0; k < extra; ++k) {
        if (p >= end) return kReplacement;
        const unsigned char b = (unsigned char)*p;
        unsigned char lo = 0x80, hi = 0xBF;            // the second byte has tighter bounds for some leads
        if (k == 0) {
            if (c == 0xE0) lo = 0xA0;                  // no overlong 3-byte
            else if (c == 0xED) hi = 0x9F;             // no surrogates
            else if (c == 0xF0) lo = 0x90;             // no overlong 4-byte
            else if (c == 0xF4) hi = 0x8F;             // nothing above U+10FFFF
        }
        if (b < lo || b > hi) return kReplacement;
        ++p;
        cp = (cp << 6) | (b & 0x3F);
    }
    return cp;
}

/// The code point at byte `i` of `s`, advancing `i` past it.
inline std::uint32_t Next(const std::string& s, std::size_t& i) {
    if (i >= s.size()) { i = s.size(); return 0; }
    const char* p = s.data() + i;
    const std::uint32_t cp = Next(p, s.data() + s.size());
    i = (std::size_t)(p - s.data());
    return cp;
}

/// Code points in `s`.
inline std::size_t Length(const std::string& s) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ++n) Next(s, i);
    return n;
}

/// Byte offset where code point `n` starts (s.size() when `s` is shorter).
inline std::size_t Offset(const std::string& s, std::size_t n) {
    std::size_t i = 0;
    for (std::size_t k = 0; k < n && i < s.size(); ++k) Next(s, i);
    return i;
}

/// Append `cp` encoded as UTF-8. Values that are not a Unicode scalar (above U+10FFFF
/// or a surrogate, U+D800..DFFF) are written as U+FFFD: encoding a surrogate as its
/// three bytes made text that no decoder, this one included, reads back as one character.
inline void Append(std::string& out, std::uint32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = kReplacement;
    if (cp < 0x80) { out += (char)cp; return; }
    if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); return; }
    if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
        return;
    }
    out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
    out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
}

/// True when every byte of `s` is ASCII.
inline bool IsAscii(const std::string& s) {
    for (char c : s) if ((unsigned char)c >= 0x80) return false;
    return true;
}

// ---- checking and repairing ----------------------------------------------------------------

/// The code point at `p` like Next, and whether the bytes were well formed. A genuine U+FFFD
/// (EF BF BD) is valid; a broken sequence, which Next also reads as U+FFFD, is not.
inline std::uint32_t NextChecked(const char*& p, const char* end, bool& valid) {
    const char* start = p;
    const std::uint32_t cp = Next(p, end);
    valid = p > start && !(cp == kReplacement && !(p - start == 3 && (unsigned char)start[0] == 0xEF &&
                                                    (unsigned char)start[1] == 0xBF && (unsigned char)start[2] == 0xBD));
    return cp;
}

/// True when every byte of `s` belongs to a well-formed sequence: no stray continuation byte, no
/// overlong form, no surrogate, nothing above U+10FFFF, nothing cut short. The empty string is valid.
inline bool IsValid(const char* data, std::size_t size) {
    const char* p = data;
    const char* end = data + size;
    while (p < end) {
        if ((unsigned char)*p < 0x80) { ++p; continue; }   // the common case, without decoding
        bool ok = true;
        NextChecked(p, end, ok);
        if (!ok) return false;
    }
    return true;
}
inline bool IsValid(const std::string& s) { return IsValid(s.data(), s.size()); }

/// `s` with every malformed sequence replaced by one U+FFFD per Next's reading (a well-formed
/// string comes back unchanged). Use it on text from a file, a socket or a text field before you
/// show it or store it.
inline std::string Sanitize(const std::string& s) {
    if (IsValid(s)) return s;
    std::string out;
    out.reserve(s.size() + 8);
    const char* p = s.data();
    const char* end = p + s.size();
    while (p < end) Append(out, Next(p, end));
    return out;
}

// ---- walking backwards, cutting --------------------------------------------------------------

/// Step back over one code point and return it (a text field's cursor left, its backspace).
/// `i` is a byte offset (past the end is the end); at 0 it returns 0 and stays. It reads the
/// same code points as walking forward with Next, malformed bytes included, so a backwards
/// walk visits the pieces of a forward walk in reverse order.
inline std::uint32_t Prev(const std::string& s, std::size_t& i) {
    if (i > s.size()) i = s.size();
    if (i == 0) return 0;
    std::size_t j = i - 1;
    for (int steps = 0; j > 0 && steps < 3 && ((unsigned char)s[j] & 0xC0) == 0x80; ++steps) --j;
    std::size_t k = j;
    const std::uint32_t cp = Next(s, k);
    if (k == i) { i = j; return cp; }
    --i;                       // the bytes before `i` are not one sequence that ends here: a lone broken byte
    return kReplacement;
}

/// The first `count` code points of `s` after skipping `start` of them (clamped to the string).
inline std::string Substr(const std::string& s, std::size_t start, std::size_t count = (std::size_t)-1) {
    const std::size_t from = Offset(s, start);
    std::size_t i = from;
    for (std::size_t k = 0; k < count && i < s.size(); ++k) Next(s, i);
    return s.substr(from, i - from);
}

/// At most `maxCodePoints` code points of `s`.
inline std::string Truncate(const std::string& s, std::size_t maxCodePoints) { return Substr(s, 0, maxCodePoints); }

/// The longest prefix of `s` that is at most `maxBytes` long and does not cut a character in
/// half (a name that must fit a 32 byte network field, a save slot label).
inline std::string TruncateBytes(const std::string& s, std::size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    std::size_t i = 0, good = 0;
    while (i < s.size()) {
        Next(s, i);
        if (i > maxBytes) break;
        good = i;
    }
    return s.substr(0, good);
}

// ---- UTF-16, UTF-32 and wide strings ------------------------------------------------------------

/// The text as UTF-16. A malformed sequence becomes U+FFFD.
inline std::u16string ToUtf16(const std::string& s) {
    std::u16string out;
    out.reserve(s.size());
    const char* p = s.data();
    const char* end = p + s.size();
    while (p < end) {
        const std::uint32_t cp = Next(p, end);
        if (cp >= 0x10000) {
            const std::uint32_t v = cp - 0x10000;
            out += (char16_t)(0xD800 + (v >> 10));
            out += (char16_t)(0xDC00 + (v & 0x3FF));
        } else {
            out += (char16_t)cp;
        }
    }
    return out;
}

/// UTF-16 to UTF-8. A surrogate that has no partner becomes U+FFFD.
inline std::string FromUtf16(const std::u16string& w) {
    std::string out;
    out.reserve(w.size());
    for (std::size_t i = 0; i < w.size(); ++i) {
        const std::uint32_t u = w[i];
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < w.size() && w[i + 1] >= 0xDC00 && w[i + 1] <= 0xDFFF) {
            Append(out, 0x10000 + ((u - 0xD800) << 10) + ((std::uint32_t)w[i + 1] - 0xDC00));
            ++i;
        } else {
            Append(out, u);   // a lone surrogate is not a scalar: Append writes U+FFFD
        }
    }
    return out;
}

/// The text as UTF-32 (one element per code point). A malformed sequence becomes U+FFFD.
inline std::u32string ToUtf32(const std::string& s) {
    std::u32string out;
    out.reserve(s.size());
    const char* p = s.data();
    const char* end = p + s.size();
    while (p < end) out += (char32_t)Next(p, end);
    return out;
}

/// UTF-32 to UTF-8. A value that is not a Unicode scalar becomes U+FFFD.
inline std::string FromUtf32(const std::u32string& w) {
    std::string out;
    out.reserve(w.size());
    for (char32_t c : w) Append(out, (std::uint32_t)c);
    return out;
}

/// The platform's wide string (std::wstring): UTF-16 where wchar_t is 16 bits (Windows API calls
/// that take LPCWSTR), UTF-32 elsewhere.
inline std::wstring ToWide(const std::string& s) {
    if constexpr (sizeof(wchar_t) == 2) {
        const std::u16string u = ToUtf16(s);
        return std::wstring(u.begin(), u.end());
    } else {
        const std::u32string u = ToUtf32(s);
        return std::wstring(u.begin(), u.end());
    }
}
inline std::string FromWide(const std::wstring& w) {
    if constexpr (sizeof(wchar_t) == 2) return FromUtf16(std::u16string(w.begin(), w.end()));
    else                                return FromUtf32(std::u32string(w.begin(), w.end()));
}

} // namespace okay::utf8
