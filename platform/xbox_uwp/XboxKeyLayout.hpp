#pragma once
// Keyboard layouts other than US for text input and key names (pure, no Windows headers; tested on Linux by
// tests/test_xbox_uwp_pieces.cpp with hand-computed values).
//
// CoreWindow gives the app a PHYSICAL key (virtual key / scan code), not the character; the engine's platform::Key is the physical position
// in the US layout (its numbers are USB HID usage ids: A = 4 ... Z = 29, 1..9 = 30..38, 0 = 39, minus 45 ... slash 56, the extra key of an
// ISO keyboard 100). This table says what that physical position types on a few common layouts:
//   AZERTY (French), QWERTZ (German), Nordic (the Swedish / Finnish layout; Norwegian and Danish differ in two keys and are NOT covered,
//   they fall back to US), Russian (ЙЦУКЕН), and US.
// Decode(layout, hid, shift, altGr, capsLock) -> the character (UTF-32, 0 = types nothing); KeyForChar is its inverse (the engine's
// Backend::KeyForChar), KeyName gives the label for a key rebinding screen. Dead keys (^ and ¨ then a vowel) are NOT composed: a dead key
// types its own mark. Caps Lock swaps the case of letters only.
// Written from the public keyboard layout charts (Microsoft keyboard layout documentation); every row below is our own data. UNVERIFIED
// against a real keyboard, a layout switch while the app runs, and a console.
#include <cstdint>
#include <string>

namespace okay::xbox {

enum class KeyLayout { US, French, German, Nordic, Russian };

namespace keylayout_detail {

struct Row { int hid; char32_t n, s, a; };   // normal, shifted (0 = upper case of normal), AltGr (0 = nothing)

inline bool IsLetter(char32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7) || (c >= 0x430 && c <= 0x44F) || c == 0x451;
}
inline char32_t Upper(char32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c == 0x451) return 0x401;
    return c;
}

// US base for the physical keys we cover.
inline Row UsRow(int hid) {
    static const char kShiftDigits[] = ")!@#$%^&*(";
    if (hid >= 4 && hid <= 29) return {hid, char32_t('a' + hid - 4), char32_t('A' + hid - 4), 0};
    if (hid >= 30 && hid <= 38) return {hid, char32_t('1' + hid - 30), (char32_t)kShiftDigits[1 + hid - 30], 0};
    switch (hid) {
        case 39: return {hid, '0', ')', 0};   case 44: return {hid, ' ', ' ', 0};
        case 45: return {hid, '-', '_', 0};   case 46: return {hid, '=', '+', 0};
        case 47: return {hid, '[', '{', 0};   case 48: return {hid, ']', '}', 0};
        case 49: return {hid, '\\', '|', 0};  case 51: return {hid, ';', ':', 0};
        case 52: return {hid, '\'', '"', 0};  case 53: return {hid, '`', '~', 0};
        case 54: return {hid, ',', '<', 0};   case 55: return {hid, '.', '>', 0};
        case 56: return {hid, '/', '?', 0};
        default: return {hid, 0, 0, 0};
    }
}

struct Table { const Row* rows; int count; };

#define OKAY_ROWS(name, ...) static const Row name[] = { __VA_ARGS__ }; return {name, (int)(sizeof(name) / sizeof(name[0]))}

inline Table Overrides(KeyLayout l) {
    switch (l) {
        case KeyLayout::French: {
            OKAY_ROWS(fr,
                {4, 'q', 0, 0}, {20, 'a', 0, 0}, {26, 'z', 0, 0}, {29, 'w', 0, 0}, {16, ',', '?', 0}, {51, 'm', 0, 0},
                {30, '&', '1', 0}, {31, 0xE9, '2', '~'}, {32, '"', '3', '#'}, {33, '\'', '4', '{'}, {34, '(', '5', '['},
                {35, '-', '6', '|'}, {36, 0xE8, '7', '`'}, {37, '_', '8', '\\'}, {38, 0xE7, '9', '^'}, {39, 0xE0, '0', '@'},
                {45, ')', 0xB0, ']'}, {46, '=', '+', '}'}, {47, '^', 0xA8, 0}, {48, '$', 0xA3, 0xA4},
                {52, 0xF9, '%', 0}, {49, '*', 0xB5, 0}, {54, ';', '.', 0}, {55, ':', '/', 0}, {56, '!', 0xA7, 0},
                {53, 0xB2, 0xB2, 0}, {100, '<', '>', 0});
        }
        case KeyLayout::German: {
            OKAY_ROWS(de,
                {28, 'z', 0, 0}, {29, 'y', 0, 0},
                {31, '2', '"', 0xB2}, {32, '3', 0xA7, 0xB3}, {33, '4', '$', 0}, {34, '5', '%', 0}, {35, '6', '&', 0},
                {36, '7', '/', '{'}, {37, '8', '(', '['}, {38, '9', ')', ']'}, {39, '0', '=', '}'},
                {45, 0xDF, '?', '\\'}, {46, 0xB4, '`', 0}, {47, 0xFC, 0, 0}, {48, '+', '*', '~'},
                {51, 0xF6, 0, 0}, {52, 0xE4, 0, 0}, {49, '#', '\'', 0}, {53, '^', 0xB0, 0},
                {54, ',', ';', 0}, {55, '.', ':', 0}, {56, '-', '_', 0}, {100, '<', '>', '|'});
        }
        case KeyLayout::Nordic: {
            OKAY_ROWS(se,
                {31, '2', '"', '@'}, {32, '3', '#', 0xA3}, {33, '4', 0xA4, '$'}, {34, '5', '%', 0}, {35, '6', '&', 0},
                {36, '7', '/', '{'}, {37, '8', '(', '['}, {38, '9', ')', ']'}, {39, '0', '=', '}'},
                {45, '+', '?', '\\'}, {46, 0xB4, '`', 0}, {47, 0xE5, 0, 0}, {48, 0xA8, '^', '~'},
                {51, 0xF6, 0, 0}, {52, 0xE4, 0, 0}, {49, '\'', '*', 0}, {53, 0xA7, 0xBD, 0},
                {54, ',', ';', 0}, {55, '.', ':', 0}, {56, '-', '_', 0}, {100, '<', '>', '|'});
        }
        case KeyLayout::Russian: {
            OKAY_ROWS(ru,
                {20, 0x439, 0, 0}, {26, 0x446, 0, 0}, {8, 0x443, 0, 0}, {21, 0x43A, 0, 0}, {23, 0x435, 0, 0}, {28, 0x43D, 0, 0},
                {24, 0x433, 0, 0}, {12, 0x448, 0, 0}, {18, 0x449, 0, 0}, {19, 0x437, 0, 0}, {47, 0x445, 0, 0}, {48, 0x44A, 0, 0},
                {4, 0x444, 0, 0}, {22, 0x44B, 0, 0}, {7, 0x432, 0, 0}, {9, 0x430, 0, 0}, {10, 0x43F, 0, 0}, {11, 0x440, 0, 0},
                {13, 0x43E, 0, 0}, {14, 0x43B, 0, 0}, {15, 0x434, 0, 0}, {51, 0x436, 0, 0}, {52, 0x44D, 0, 0},
                {29, 0x44F, 0, 0}, {27, 0x447, 0, 0}, {6, 0x441, 0, 0}, {25, 0x43C, 0, 0}, {5, 0x438, 0, 0}, {17, 0x442, 0, 0},
                {16, 0x44C, 0, 0}, {54, 0x431, 0, 0}, {55, 0x44E, 0, 0}, {53, 0x451, 0, 0},
                {31, '2', '"', 0}, {32, '3', 0x2116, 0}, {33, '4', ';', 0}, {34, '5', '%', 0}, {35, '6', ':', 0},
                {36, '7', '?', 0}, {37, '8', '*', 0}, {38, '9', '(', 0}, {39, '0', ')', 0},
                {49, '\\', '/', 0}, {56, '.', ',', 0});
        }
        default: return {nullptr, 0};
    }
}
#undef OKAY_ROWS

inline Row Lookup(KeyLayout l, int hid) {
    const Table t = Overrides(l);
    for (int i = 0; i < t.count; ++i) if (t.rows[i].hid == hid) {
        Row r = t.rows[i];
        if (r.s == 0 && IsLetter(r.n)) r.s = Upper(r.n);
        else if (r.s == 0) r.s = r.n;
        return r;
    }
    return UsRow(hid);
}

} // namespace keylayout_detail

/// The character a physical key types. `hid` is the platform::Key number. 0 = nothing (or a key not in the table).
inline char32_t DecodeKey(KeyLayout l, int hid, bool shift, bool altGr = false, bool capsLock = false) {
    using namespace keylayout_detail;
    const Row r = Lookup(l, hid);
    if (altGr) return r.a;
    if (r.n == 0) return 0;
    const bool upper = IsLetter(r.n) ? (shift != capsLock) : shift;
    return upper ? r.s : r.n;
}

/// The physical key (platform::Key number) that types `c` on this layout (plain or with Shift); 0 when none does.
inline int KeyForChar(KeyLayout l, char32_t c) {
    using namespace keylayout_detail;
    if (c == 0) return 0;
    for (int hid = 4; hid <= 100; ++hid) {
        if (hid > 56 && hid != 100) continue;
        const Row r = Lookup(l, hid);
        if (r.n == c || (r.s == c && r.s != 0)) return hid;
    }
    return 0;
}

inline std::string EncodeUtf8(char32_t c) {
    std::string o;
    if (c < 0x80) o += (char)c;
    else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    return o;
}

/// The label for a key on a rebinding screen: the upper-case form of what it types (UTF-8), "Space" for the space bar, "" when unknown.
inline std::string KeyName(KeyLayout l, int hid) {
    if (hid == 44) return "Space";
    const char32_t c = DecodeKey(l, hid, false);
    return c == 0 ? std::string() : EncodeUtf8(keylayout_detail::Upper(c));
}

/// From a BCP-47 tag such as CoreWindow's input language ("fr-FR", "de-AT", "sv-SE", "ru-RU"); anything else is US.
inline KeyLayout LayoutFromLanguageTag(const std::string& tag) {
    std::string lang;
    for (char c : tag) { if (c == '-' || c == '_') break; lang += (char)((c >= 'A' && c <= 'Z') ? c + 32 : c); }
    std::string region;
    const std::size_t d = tag.find_first_of("-_");
    if (d != std::string::npos) for (std::size_t i = d + 1; i < tag.size(); ++i) { const char c = tag[i]; region += (char)((c >= 'a' && c <= 'z') ? c - 32 : c); }
    if (lang == "fr" && region != "CA" && region != "CH") return KeyLayout::French;   // Canadian and Swiss French layouts differ
    if (lang == "de" && region != "CH") return KeyLayout::German;
    if (lang == "sv" || lang == "fi") return KeyLayout::Nordic;
    if (lang == "ru") return KeyLayout::Russian;
    return KeyLayout::US;
}

} // namespace okay::xbox
