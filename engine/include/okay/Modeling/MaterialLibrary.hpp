#pragma once
// A small library of ready colours (for material slots) and slot palettes (several slots at once),
// so a new model does not start from white. These are plain colours chosen here, not copied from any
// asset or product; nothing to attribute.
#include "okay/Render/Color.hpp"
#include <string>
#include <vector>

namespace okay::modeling {

struct LibraryMaterial {
    const char* name;
    const char* group;      ///< Wood, Stone, Metal, Cloth, Nature, Paint, Glow
    Color color;
};
struct SlotPalette {
    const char* name;
    std::vector<LibraryMaterial> slots;   ///< slot 0, 1, 2, ... in order
};

/// Every library material (never empty, names unique).
inline const std::vector<LibraryMaterial>& MaterialLibrary() {
    static const std::vector<LibraryMaterial> lib = {
        {"Pale Wood",    "Wood",   Color{0.80f, 0.66f, 0.45f, 1}}, {"Oak",          "Wood",   Color{0.55f, 0.38f, 0.20f, 1}},
        {"Dark Wood",    "Wood",   Color{0.28f, 0.17f, 0.10f, 1}}, {"Plank",        "Wood",   Color{0.68f, 0.50f, 0.30f, 1}},
        {"Granite",      "Stone",  Color{0.50f, 0.50f, 0.52f, 1}}, {"Sandstone",    "Stone",  Color{0.82f, 0.72f, 0.52f, 1}},
        {"Brick",        "Stone",  Color{0.60f, 0.27f, 0.20f, 1}}, {"Slate",        "Stone",  Color{0.28f, 0.31f, 0.36f, 1}},
        {"Concrete",     "Stone",  Color{0.66f, 0.66f, 0.64f, 1}}, {"Marble",       "Stone",  Color{0.92f, 0.92f, 0.90f, 1}},
        {"Steel",        "Metal",  Color{0.66f, 0.69f, 0.73f, 1}}, {"Iron",         "Metal",  Color{0.30f, 0.31f, 0.33f, 1}},
        {"Copper",       "Metal",  Color{0.72f, 0.45f, 0.28f, 1}}, {"Gold",         "Metal",  Color{0.90f, 0.72f, 0.25f, 1}},
        {"Rust",         "Metal",  Color{0.55f, 0.28f, 0.14f, 1}}, {"Bronze",       "Metal",  Color{0.60f, 0.45f, 0.22f, 1}},
        {"Canvas",       "Cloth",  Color{0.86f, 0.82f, 0.70f, 1}}, {"Denim",        "Cloth",  Color{0.22f, 0.32f, 0.52f, 1}},
        {"Leather",      "Cloth",  Color{0.40f, 0.25f, 0.15f, 1}}, {"Velvet",       "Cloth",  Color{0.45f, 0.10f, 0.20f, 1}},
        {"Grass",        "Nature", Color{0.30f, 0.60f, 0.25f, 1}}, {"Leaf",         "Nature", Color{0.18f, 0.45f, 0.18f, 1}},
        {"Soil",         "Nature", Color{0.36f, 0.26f, 0.18f, 1}}, {"Sand",         "Nature", Color{0.90f, 0.82f, 0.60f, 1}},
        {"Snow",         "Nature", Color{0.96f, 0.97f, 1.00f, 1}}, {"Water",        "Nature", Color{0.20f, 0.45f, 0.75f, 1}},
        {"White Paint",  "Paint",  Color{0.95f, 0.95f, 0.94f, 1}}, {"Black Paint",  "Paint",  Color{0.08f, 0.08f, 0.09f, 1}},
        {"Red Paint",    "Paint",  Color{0.78f, 0.14f, 0.12f, 1}}, {"Blue Paint",   "Paint",  Color{0.15f, 0.30f, 0.75f, 1}},
        {"Yellow Paint", "Paint",  Color{0.95f, 0.80f, 0.15f, 1}}, {"Green Paint",  "Paint",  Color{0.18f, 0.60f, 0.25f, 1}},
        {"Orange Paint", "Paint",  Color{0.93f, 0.50f, 0.12f, 1}}, {"Purple Paint", "Paint",  Color{0.50f, 0.22f, 0.65f, 1}},
        {"Glass",        "Glow",   Color{0.70f, 0.85f, 0.90f, 1}}, {"Lamp Light",   "Glow",   Color{1.00f, 0.92f, 0.65f, 1}},
    };
    return lib;
}

/// Slot palettes: pick one and slots 0.. take its colours.
inline const std::vector<SlotPalette>& SlotPalettes() {
    const auto& L = MaterialLibrary();
    auto by = [&](const char* n) { for (const auto& m : L) if (std::string(m.name) == n) return m; return L[0]; };
    static const std::vector<SlotPalette> pal = {
        {"House",        {by("Plank"), by("Brick"), by("Slate"), by("Oak")}},
        {"Castle",       {by("Granite"), by("Slate"), by("Dark Wood"), by("Iron")}},
        {"Barn",         {by("Red Paint"), by("White Paint"), by("Dark Wood"), by("Rust")}},
        {"Workshop",     {by("Steel"), by("Iron"), by("Pale Wood"), by("Rust")}},
        {"Forest",       {by("Soil"), by("Leaf"), by("Grass"), by("Oak")}},
        {"Desert",       {by("Sand"), by("Sandstone"), by("Rust"), by("Canvas")}},
        {"Winter",       {by("Snow"), by("Slate"), by("Dark Wood"), by("Water")}},
        {"Primary",      {by("Red Paint"), by("Blue Paint"), by("Yellow Paint"), by("White Paint")}},
    };
    return pal;
}

inline const LibraryMaterial* FindLibraryMaterial(const std::string& name) {
    for (const auto& m : MaterialLibrary()) if (name == m.name) return &m;
    return nullptr;
}

} // namespace okay::modeling
