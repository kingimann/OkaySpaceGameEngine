#pragma once
#include "okay/Components/UIWidgetUtil.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include <vector>
#include <algorithm>

namespace okay {

/// Grey bars and a circle shown while something is loading. SetLoading(false)
/// clears them. The bars shimmer so the screen does not look frozen.
class UISkeleton final : public Behaviour {
public:
    struct Bar { float x, y, w, h; bool circle = false; };

    Vec2 position{20.0f, 20.0f};
    Vec2 size{320.0f, 96.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    bool loading = true;
    int lines = 3;
    bool showAvatar = true;
    float shimmer = 0.0f;
    bool interactable = false;
    bool focusable = false;
    Color background = Color::FromBytes(28, 32, 44);
    Color bar = Color::FromBytes(58, 64, 80);
    Color highlight = Color::FromBytes(96, 106, 128);
    UIShape shape = UIShape::Rounded;
    float cornerRadius = 6.0f;

    bool IsFocused() const { return false; }
    void SetFocused(bool) {}

    void SetLoading(bool on) {
        if (loading == on) return;
        loading = on;
        FireUIEvent(gameObject, "on_change");
    }

    std::vector<Bar> Bars() const {
        std::vector<Bar> out;
        if (!loading) return out;
        const float pad = 12.0f;
        float textX = pad;
        if (showAvatar) {
            const float d = std::min(40.0f, std::max(8.0f, size.y - pad * 2.0f));
            out.push_back({pad, (size.y - d) * 0.5f, d, d, true});
            textX = pad + d + 12.0f;
        }
        const int n = lines < 1 ? 1 : (lines > 8 ? 8 : lines);
        const float gap = 8.0f;
        const float h = 12.0f;
        const float block = n * h + (n - 1) * gap;
        float y = (size.y - block) * 0.5f;
        if (y < 8.0f) y = 8.0f;
        const float full = std::max(8.0f, size.x - textX - pad);
        const float scale[3] = {1.0f, 0.86f, 0.62f};
        for (int i = 0; i < n; ++i) {
            if (y + h > size.y) break;
            out.push_back({textX, y, full * scale[i % 3], h, false});
            y += h + gap;
        }
        return out;
    }
    int PlaceholderCount() const { return (int)Bars().size(); }

    void Tick(float dt) {
        if (!loading) return;
        shimmer += dt * 0.65f;
        if (shimmer > 1.0f) shimmer -= 1.0f;
        if (shimmer < 0.0f) shimmer = 0.0f;
    }

    void Update(float dt) override { if (enabled) Tick(dt); }

private:
};

} // namespace okay
