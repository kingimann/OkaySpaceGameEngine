#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace okay {

/// A screen-space texture drawn through a UV rectangle: show
/// part of an image, tile it (UV size above 1 repeats it), or scroll it (a moving
/// background, a conveyor, a minimap). No slicing or fills: the texture as is.
class UIRawImage : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{256.0f, 256.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    std::string texture;                 ///< image path (PNG/JPG/BMP)
    Color color = Color::White;          ///< tint
    /// Which part of the texture fills the rect: (x, y) offset and (w, h) size in
    /// texture units. (0, 0, 1, 1) is the whole image; (0, 0, 3, 2) tiles it 3 x 2.
    float uvX = 0.0f, uvY = 0.0f, uvW = 1.0f, uvH = 1.0f;
    /// UV units per second added to the offset (a scrolling background).
    Vec2 scroll{0.0f, 0.0f};

    void Update(float dt) override {
        if (scroll.x == 0.0f && scroll.y == 0.0f) return;
        uvX = std::fmod(uvX + scroll.x * dt, 1.0f);
        uvY = std::fmod(uvY + scroll.y * dt, 1.0f);
    }

    /// One piece of the drawn rect and the part of the texture it shows (both
    /// 0..1 inside a single copy of the image), so hosts without texture
    /// wrapping can tile by drawing each piece.
    struct Piece { float x, y, w, h; float u0, v0, u1, v1; };

    /// Split a `w` x `h` rect into pieces for the current UV rect. A flipped UV
    /// (negative size) mirrors the image. At most 64 x 64 pieces.
    std::vector<Piece> Pieces(float w, float h) const {
        std::vector<Piece> out;
        if (w <= 0 || h <= 0 || uvW == 0.0f || uvH == 0.0f) return out;
        auto spans = [](float start, float len, std::vector<std::pair<float, float>>& s) {
            const bool flip = len < 0;
            float a = flip ? start + len : start, b = flip ? start : start + len;
            for (float t = a; t < b - 1e-6f && s.size() < 64;) {
                const float next = std::min(b, std::floor(t) + 1.0f);
                s.push_back({t, next});
                t = next;
            }
            if (flip) std::reverse(s.begin(), s.end());
        };
        std::vector<std::pair<float, float>> us, vs;
        spans(uvX, uvW, us); spans(uvY, uvH, vs);
        const float uLen = std::fabs(uvW), vLen = std::fabs(uvH);
        float py = 0.0f;
        for (const auto& v : vs) {
            const float ph = h * (v.second - v.first) / vLen;
            float px = 0.0f;
            for (const auto& u : us) {
                const float pw = w * (u.second - u.first) / uLen;
                const float fu = std::floor(u.first), fv = std::floor(v.first);
                Piece p{px, py, pw, ph, u.first - fu, v.first - fv, u.second - fu, v.second - fv};
                if (uvW < 0) std::swap(p.u0, p.u1);
                if (uvH < 0) std::swap(p.v0, p.v1);
                out.push_back(p);
                px += pw;
            }
            py += ph;
        }
        return out;
    }
};

} // namespace okay
