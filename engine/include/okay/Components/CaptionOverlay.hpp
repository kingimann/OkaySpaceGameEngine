#pragma once
// Draws CaptionManager::Main() on screen (docs/accessibility.md). The manager
// decides the words, colours, wrap and fade; this widget only places them.
// One overlay per scene also advances the manager's clock. Player and editor
// Play call Ensure so a scene does not need the object saved into it.
#include "okay/Components/Canvas.hpp"
#include "okay/Core/Subtitles.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace okay {

class CaptionOverlay : public Behaviour {
public:
    /// One painted piece: a box behind a line, or a run of text.
    struct Mark {
        bool box = false;
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        std::string text;
        Color color = Color::White;
        bool italic = false;
        float pixel = 2.0f;   ///< DrawUIText font-pixel size (one 8x8 cell is 8*pixel wide)
    };

    int ExecutionOrder() const override { return 200; }   // after cutscene and dialogue updates

    static bool Any(const Scene* scene) {
        if (!scene) return false;
        for (auto* c : scene->FindObjectsOfType<CaptionOverlay>())
            if (c && c->enabled && c->gameObject && c->gameObject->active) return true;
        return false;
    }

    /// Add a "Captions" object when the scene has none, under the canvas when
    /// there is one. Safe to call every frame.
    static void Ensure(Scene& scene) {
        if (scene.FindObjectOfType<CaptionOverlay>()) return;
        GameObject* canvas = nullptr;
        if (Canvas* c = scene.FindObjectOfType<Canvas>()) canvas = c->gameObject;
        GameObject* g = scene.CreateGameObject("Captions");
        if (canvas && canvas->transform && g->transform) g->transform->SetParent(canvas->transform, false);
        g->AddComponent<CaptionOverlay>();
    }

    void Update(float dt) override {
        if (dt < 0.0f || !gameObject || !gameObject->active || !enabled) return;
        Scene* s = gameObject->scene();
        if (!s) return;
        for (auto* c : s->FindObjectsOfType<CaptionOverlay>()) {
            if (!c || !c->enabled || !c->gameObject || !c->gameObject->active) continue;
            if (c == this) CaptionManager::Main().Update(dt);   // the first live overlay owns the clock
            return;
        }
    }

    /// What to paint for a screen of `width` x `height`. Empty when nothing is
    /// visible (a line still fading in at alpha 0 is not drawn yet). Boxes sit
    /// at the bottom, or the top when the player chose that.
    void Layout(float width, float height, std::vector<Mark>& out) const {
        out.clear();
        if (width < 8.0f || height < 8.0f) return;
        const CaptionManager& caps = CaptionManager::Main();
        const std::vector<SubtitleDrawLine> lines = caps.Lines();
        if (lines.empty()) return;

        struct Block { std::vector<Mark> marks; float w = 0.0f; float h = 0.0f; };
        std::vector<Block> blocks;
        for (const SubtitleDrawLine& line : lines) {
            if (line.alpha < 0.02f) continue;
            const bool anyText = !line.speakerLabel.empty() || !line.rows.empty();
            if (!anyText) continue;
            const float px = 2.0f * (line.scale > 0.05f ? line.scale : 1.0f);
            const float cw = 8.0f * px;
            const float ch = 8.0f * px;
            const float pad = 4.0f * px;
            auto cells = [](const std::string& s) { return (float)CaptionManager::Cells(s); };
            Block b;
            float y = pad;
            float maxW = 0.0f;
            auto addText = [&](const std::string& text, float x, float ty, const Color& col, bool italic) {
                if (text.empty()) return;
                Mark m;
                m.x = x; m.y = ty; m.pixel = px; m.text = text; m.color = col; m.italic = italic;
                b.marks.push_back(std::move(m));
            };
            const float labelW = cells(line.speakerLabel) * cw;
            if (!line.rows.empty()) {
                addText(line.speakerLabel, pad, y, line.speakerColour, false);
                addText(line.rows.front(), pad + labelW, y, line.textColour, line.italic);
                maxW = std::max(maxW, pad + labelW + cells(line.rows.front()) * cw + pad);
                y += ch;
                for (std::size_t i = 1; i < line.rows.size(); ++i) {
                    addText(line.rows[i], pad, y, line.textColour, line.italic);
                    maxW = std::max(maxW, pad + cells(line.rows[i]) * cw + pad);
                    y += ch;
                }
            } else {
                addText(line.speakerLabel, pad, y, line.speakerColour, false);
                maxW = pad + labelW + pad;
                y += ch;
            }
            b.w = std::min(width * 0.9f, std::max(maxW, ch));
            b.h = y + pad;
            Mark box;
            box.box = true; box.w = b.w; box.h = b.h; box.color = line.backgroundColour;
            b.marks.insert(b.marks.begin(), box);
            blocks.push_back(std::move(b));
        }
        if (blocks.empty()) return;
        const float gap = 8.0f;
        float totalH = 0.0f;
        for (const Block& b : blocks) totalH += b.h;
        totalH += gap * (float)(blocks.size() - 1);
        float y = caps.settings.top ? height * 0.06f : height * 0.92f - totalH;
        for (Block& b : blocks) {
            const float x = (width - b.w) * 0.5f;
            for (Mark& m : b.marks) { m.x += x; m.y += y; }
            out.insert(out.end(), b.marks.begin(), b.marks.end());
            y += b.h + gap;
        }
    }
};

} // namespace okay
