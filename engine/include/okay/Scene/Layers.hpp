#pragma once
#include <array>
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <string>

namespace okay {

/// named layers. There are 32 layers (0-31); a GameObject sits on one
/// (GameObject::layer) and a Camera renders a set of them (Camera::cullingMask, a
/// 32-bit mask). Layer names are global project settings, editable in the editor
/// (ProjectLists saves and loads them with the project settings). Main thread only.
struct Layers {
    static constexpr int kCount = 32;
    static constexpr std::size_t kMaxNameLength = 32;
    static bool IsValid(int i) { return i >= 0 && i < kCount; }
    /// The name a layer starts with (empty for the layers a new project leaves unnamed).
    static const char* DefaultName(int i) {
        switch (i) { case 0: return "Default"; case 1: return "TransparentFX"; case 2: return "Ignore Raycast";
                     case 4: return "Water"; case 5: return "UI"; default: return ""; }
    }
    /// Whether layer `i` has a name of its own (rather than showing as "Layer N").
    static bool IsNamed(int i) { return IsValid(i) && !Names()[static_cast<std::size_t>(i)].empty(); }
    /// A name a layer can take: 1 to 32 characters, no control characters, no leading or
    /// trailing space, no ',' or '!' (they are mask-list syntax) and not only digits (a number
    /// reads as a layer index).
    static bool IsValidName(const std::string& n) {
        if (n.empty() || n.size() > kMaxNameLength) return false;
        if (n.front() == ' ' || n.back() == ' ') return false;
        bool digits = true;
        for (unsigned char c : n) {
            if (c < 0x20 || c == 0x7F || c == ',' || c == '!') return false;
            if (c < '0' || c > '9') digits = false;
        }
        return !digits;
    }
    /// The layer called `name` (case and surrounding space do not matter). A layer's display name
    /// ("Layer 7") and a bare number are read too. -1 when there is no such layer.
    static int NameToLayer(const std::string& name) {
        const std::size_t b = name.find_first_not_of(" \t");
        if (b == std::string::npos) return -1;
        const std::string t = name.substr(b, name.find_last_not_of(" \t") - b + 1);
        auto lower = [](std::string x) { for (char& ch : x) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); return x; };
        const std::string want = lower(t);
        bool digits = true;
        for (char c : t) if (c < '0' || c > '9') { digits = false; break; }
        if (digits) return t.size() <= 2 && std::atoi(t.c_str()) < kCount ? std::atoi(t.c_str()) : -1;
        for (int i = 0; i < kCount; ++i) if (lower(Name(i)) == want) return i;
        return -1;
    }
    /// Give layer `i` a name. False (nothing changes) when `i` is not 0-31, the name is not valid
    /// (IsValidName) or another layer already has it (case does not matter). Layer 0 can be renamed.
    static bool SetName(int i, const std::string& name) {
        if (!IsValid(i) || !IsValidName(name)) return false;
        const int other = NameToLayer(name);
        if (other >= 0 && other != i) return false;
        Names()[static_cast<std::size_t>(i)] = name;
        return true;
    }
    /// Remove a layer's name (it shows as "Layer N" again). Layer 0 always keeps a name.
    static bool ClearName(int i) {
        if (!IsValid(i) || i == 0) return false;
        Names()[static_cast<std::size_t>(i)].clear();
        return true;
    }
    /// Every layer back to the names a new project has.
    static void ResetNames() {
        for (int i = 0; i < kCount; ++i) Names()[static_cast<std::size_t>(i)] = DefaultName(i);
    }
    /// A mask with the given layers set (numbers outside 0-31 add nothing).
    static std::uint32_t Mask(std::initializer_list<int> layers) {
        std::uint32_t m = 0;
        for (int l : layers) if (IsValid(l)) m |= (1u << l);
        return m;
    }
    /// The inverse of MaskFromList: names (or "Layer N") joined with ", ". A mask with every bit
    /// set is "" (blank means every layer there). An empty mask is "none", which only reads well:
    /// MaskFromList does not take it back (nothing valid means every layer).
    static std::string MaskToList(std::uint32_t mask) {
        if (mask == 0xFFFFFFFFu) return "";
        if (mask == 0) return "none";
        std::string out;
        for (int i = 0; i < kCount; ++i)
            if ((mask >> i) & 1u) { if (!out.empty()) out += ", "; out += Name(i); }
        return out;
    }

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
                    // Names match without regard to case ("default" finds "Default").
                    auto lower = [](std::string s) { for (char& ch : s) ch = (char)std::tolower((unsigned char)ch); return s; };
                    const std::string want = lower(tok);
                    for (int i = 0; i < 32; ++i) if (lower(Name(i)) == want) { mask |= (1u << i); break; }
                }
            }
            start = end + 1;
        }
        if (mask == 0) return 0xFFFFFFFFu;
        return invert ? ~mask : mask;
    }
};

} // namespace okay
