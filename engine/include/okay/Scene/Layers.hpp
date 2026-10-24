#pragma once
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace okay {

/// Unity-style named layers. There are 32 layers (0-31); a GameObject sits on one
/// (GameObject::layer) and a Camera renders a set of them (Camera::cullingMask, a
/// 32-bit mask). Layer names are global project settings, editable in the editor.
struct Layers {
    static std::array<std::string, 32>& Names() {
        static std::array<std::string, 32> n = [] {
            std::array<std::string, 32> a{};
            a[0] = "Default";
            a[1] = "TransparentFX";
            a[2] = "Ignore Raycast";
            a[4] = "Water";
            a[5] = "UI";
            return a;
        }();
        return n;
    }
    /// Display name for a layer index (falls back to "Layer N" when unnamed).
    static std::string Name(int i) {
        i &= 31;
        const std::string& s = Names()[i];
        return s.empty() ? ("Layer " + std::to_string(i)) : s;
    }
    /// The bit for a layer index.
    static int Bit(int i) { return 1 << (i & 31); }
    /// A physics query layer mask from a friendly list: layer numbers and/or names
    /// separated by commas ("0, 3" or "Default,Enemy"). Blank (or nothing valid) =
    /// every layer. A leading '!' inverts it ("!Ignore Raycast" = all but that).
    static std::uint32_t MaskFromList(const std::string& list) {
        std::string s = list;
        bool invert = false;
        std::size_t a = s.find_first_not_of(" \t");
        if (a != std::string::npos && s[a] == '!') { invert = true; s.erase(0, a + 1); }
        std::uint32_t mask = 0;
        std::size_t start = 0;
        while (start <= s.size()) {
            std::size_t end = s.find(',', start);
            if (end == std::string::npos) end = s.size();
            std::string tok = s.substr(start, end - start);
            std::size_t b = tok.find_first_not_of(" \t"), e = tok.find_last_not_of(" \t");
            tok = b == std::string::npos ? std::string() : tok.substr(b, e - b + 1);
            if (!tok.empty()) {
                bool digits = true;
                for (char c : tok) if (c < '0' || c > '9') { digits = false; break; }
                if (digits && tok.size() <= 2) {
                    int i = std::atoi(tok.c_str());
                    if (i >= 0 && i < 32) mask |= (1u << i);
                } else {
                    for (int i = 0; i < 32; ++i) if (Name(i) == tok) { mask |= (1u << i); break; }
                }
            }
            start = end + 1;
        }
        if (mask == 0) return 0xFFFFFFFFu;
        return invert ? ~mask : mask;
    }
};

} // namespace okay
