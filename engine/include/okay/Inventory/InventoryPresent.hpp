#pragma once
// Shared inventory presentation: labels that don't split a character, a colour
// tile when an icon file is missing, and an optional atlas rectangle on the
// item definition. Drawing stays in the player and the editor.
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <string>

namespace okay {

/// How many bytes the UTF-8 sequence at `lead` occupies (1 when it isn't a lead).
inline int Utf8SeqLen(unsigned char lead) {
    if ((lead & 0x80u) == 0) return 1;
    if ((lead & 0xE0u) == 0xC0u) return 2;
    if ((lead & 0xF0u) == 0xE0u) return 3;
    if ((lead & 0xF8u) == 0xF0u) return 4;
    return 1;
}

/// The name to draw: the definition's display name, else the stored id.
inline std::string ItemLabel(const std::string& id) {
    const ItemDefinition* d = ItemDatabase::Get().Find(id);
    return d ? d->DisplayName() : id;
}

/// Cut `text` to at most `maxChars` code points and to `maxPx` pixels at size `px`.
/// Never splits a UTF-8 sequence. `nameChars` on the UI is that character maximum.
inline std::string Ellipsize(const std::string& text, float maxPx, float px, int maxChars) {
    if (text.empty() || maxPx <= 1.0f) return {};
    if (maxChars < 1) maxChars = 1;
    if (px < 0.5f) px = 0.5f;
    std::string out;
    int chars = 0;
    for (std::size_t i = 0; i < text.size() && chars < maxChars; ) {
        int n = Utf8SeqLen((unsigned char)text[i]);
        if (i + (std::size_t)n > text.size()) n = 1;
        std::string next = out + text.substr(i, (std::size_t)n);
        if (!out.empty() && ui2d::UITextW(next, px) > maxPx) break;
        out.swap(next);
        i += (std::size_t)n;
        ++chars;
    }
    return out;
}

/// A stable tile colour for an item that has no icon file.
inline Color IconFallbackColor(const std::string& key) {
    unsigned h = 2166136261u;
    for (unsigned char c : key) { h ^= c; h *= 16777619u; }
    return Color::FromBytes((int)(40 + (h & 127)), (int)(48 + ((h >> 8) & 95)), (int)(64 + ((h >> 16) & 111)), 230);
}

/// `path` with `@2x` before the extension (`wood.png` -> `wood@2x.png`).
inline std::string HiDpiIconPath(const std::string& path) {
    const std::size_t dot = path.rfind('.');
    const std::size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return path + "@2x";
    return path.substr(0, dot) + "@2x" + path.substr(dot);
}

/// Pixel rectangle of the definition's atlas UV inside a texture of `tw` x `th`.
/// False when the definition uses the whole image.
inline bool IconPixelRect(const ItemDefinition& d, int tw, int th, int& x, int& y, int& w, int& h) {
    if (!d.hasIconRect || tw <= 0 || th <= 0) return false;
    x = (int)(d.iconU * (float)tw);
    y = (int)(d.iconV * (float)th);
    w = (int)(d.iconUW * (float)tw);
    h = (int)(d.iconUH * (float)th);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (x + w > tw) w = tw - x;
    if (y + h > th) h = th - y;
    return w > 0 && h > 0;
}

} // namespace okay
