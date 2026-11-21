#pragma once
#include <functional>
#include <string>
#include <vector>

namespace okay {
namespace InventoryText {

/// Fits `text` into `maxW` pixels using a caller-supplied width measure (the renderer's
/// UITextW or ImGui's CalcTextSize), so the logic is pure and testable without a window.
/// If the whole text fits it is returned unchanged. Otherwise the longest prefix (cut on a
/// UTF-8 code point, never inside one) that fits together with "..." is returned. If even
/// "..." does not fit the result is empty. Written from the plain problem statement.
inline std::string FitWidth(const std::string& text, float maxW,
                            const std::function<float(const std::string&)>& measure) {
    if (maxW <= 0.0f) return std::string();
    if (measure(text) <= maxW) return text;
    const std::string dots = "...";
    if (measure(dots) > maxW) return std::string();
    // cuts[k] = byte offset after the first k code points.
    std::vector<size_t> cuts{0};
    for (size_t i = 1; i <= text.size(); ++i)
        if (i == text.size() || (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) cuts.push_back(i);
    size_t lo = 0, hi = cuts.size() - 1;   // largest k whose prefix + "..." fits; k = 0 always does
    while (lo < hi) {                       // binary search, assumes width grows with length
        const size_t mid = (lo + hi + 1) / 2;
        if (measure(text.substr(0, cuts[mid]) + dots) <= maxW) lo = mid;
        else hi = mid - 1;
    }
    return text.substr(0, cuts[lo]) + dots;
}

} // namespace InventoryText
} // namespace okay
