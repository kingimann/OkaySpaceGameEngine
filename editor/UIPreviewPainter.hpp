#pragma once
// ui2d::Painter over an ImGui draw list: lets the editor's Scene/Game-view UI preview
// draw through the engine's UI renderer (okay::ui2d::RenderSceneUI), so the preview
// shows exactly the widgets, layering and styling the built game draws.
//
// Coordinates are the renderer's logical pixels, mapped to the draw list by an origin
// (the canvas's top-left on screen) and the renderer's scale. Textures are the
// editor's SDL_Textures (the ImGui SDL_Renderer backend's ImTextureID). Texture
// colour/alpha mods are kept per texture in the painter and baked into vertex
// colours: the draw list renders later, so the shared SDL textures are never touched.
#include "okay/UI/UIPainter.hpp"
#include "imgui.h"
#include <SDL.h>
#include <string>
#include <unordered_map>

namespace oked {

class UIPreviewPainter final : public okay::ui2d::Painter {
public:
    using TextureLoader = SDL_Texture* (*)(const std::string& path);

    UIPreviewPainter(ImDrawList* dl, ImVec2 origin, SDL_Renderer* renderer, TextureLoader loader);
    ~UIPreviewPainter() override;   // pops any clip rect it pushed

    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override;
    void FillRect(const okay::ui2d::Rect* r) override;
    void DrawRect(const okay::ui2d::Rect* r) override;
    void DrawLine(int x0, int y0, int x1, int y1) override;
    void DrawPoint(int x, int y) override;

    void SetClipRect(const okay::ui2d::Rect* r) override;
    void GetClipRect(okay::ui2d::Rect* r) override;
    bool IsClipEnabled() override { return m_clipOn; }
    void SetScale(float sx, float sy) override;

    okay::ui2d::Tex LoadTexture(const std::string& path) override;
    okay::ui2d::Tex FontAtlas(okay::TtfFont* font) override;
    void QueryTexture(okay::ui2d::Tex t, int* w, int* h) override;
    void SetTextureColorMod(okay::ui2d::Tex t, std::uint8_t r, std::uint8_t g, std::uint8_t b) override;
    void SetTextureAlphaMod(okay::ui2d::Tex t, std::uint8_t a) override;
    void Copy(okay::ui2d::Tex t, const okay::ui2d::Rect* src, const okay::ui2d::Rect* dst) override;
    void CopyF(okay::ui2d::Tex t, const okay::ui2d::Rect* src, const okay::ui2d::FRect* dst) override;
    void CopyEx(okay::ui2d::Tex t, const okay::ui2d::Rect* src, const okay::ui2d::Rect* dst, double angle,
                const okay::ui2d::Point* center, int flip) override;
    void CopyExF(okay::ui2d::Tex t, const okay::ui2d::Rect* src, const okay::ui2d::FRect* dst, double angle,
                 const okay::ui2d::FPoint* center, int flip) override;
    void Geometry(okay::ui2d::Tex t, const okay::ui2d::Vertex* v, int nv, const int* idx, int ni) override;
    /// Outlines the widget and labels it "missing: <path>" (an authoring aid).
    void MissingTexture(const std::string& path, const okay::ui2d::Rect& where) override;

private:
    ImVec2 P(float x, float y) const { return ImVec2(m_origin.x + x * m_scale, m_origin.y + y * m_scale); }
    void FillPx(float x, float y, float w, float h);   // filled, current draw colour
    ImU32 Tint(okay::ui2d::Tex t, ImU32 vertexColor = IM_COL32_WHITE) const;
    void Blit(okay::ui2d::Tex t, const okay::ui2d::Rect* src, float x, float y, float w, float h,
              double angle, int flip);

    ImDrawList* m_dl;
    ImVec2 m_origin;
    SDL_Renderer* m_renderer;
    TextureLoader m_loader;
    float m_scale = 1.0f;
    ImU32 m_color = IM_COL32_WHITE;
    bool m_clipOn = false, m_clipPushed = false;
    okay::ui2d::Rect m_clip{0, 0, 0, 0};
    std::unordered_map<okay::ui2d::Tex, okay::ui2d::RGBA> m_mods;
};

/// A TTF font's glyph atlas as an SDL texture for ImGui draw lists, uploaded again
/// when the font bakes new characters. Draw lists render at the end of the frame, so
/// a texture replaced because the atlas grew stays alive until two frames later.
SDL_Texture* PreviewFontAtlas(SDL_Renderer* r, okay::TtfFont* f);

} // namespace oked
