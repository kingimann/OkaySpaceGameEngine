#pragma once
#include <array>
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
};

} // namespace okay
