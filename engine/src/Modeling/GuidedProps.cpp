// Prop templates for guided modeling: furniture, container, tool and decoration. Each is a set of named parts (legs, top,
// back, lid, bands, handle, head...) that can be given another design or removed. Written from general parametric modeling
// (boxes, prisms, ellipsoids, profiles pushed across a width); no code, models or assets from other projects. Props stand on
// y = 0 and are centred on x = z = 0.
#include "GuidedKit.hpp"
#include <map>

namespace okay::modeling::guided {
namespace {
using namespace kit;

PolyMesh Cyl(float x, float y0, float y1, float z, float rx, float rz, int sides = 12) {   // an upright cylinder
    Frame f = FrameAA({x - rx, y0, z - rz}, {x + rx, y1, z + rz});
    return shapes::Prism(f, sides);
}
PolyMesh Prof(std::vector<Vec2> prof, float z0, float z1) {   // a side profile (x, y) pushed across z; the order is fixed to counter-clockwise
    double area = 0;
    for (size_t i = 0; i < prof.size(); ++i) { const Vec2& a = prof[i]; const Vec2& b = prof[(i + 1) % prof.size()]; area += (double)a.x * b.y - (double)b.x * a.y; }
    if (area < 0) std::reverse(prof.begin(), prof.end());
    return shapes::ExtrudeAxes({0, 0, (z0 + z1) * 0.5f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, prof, z1 - z0);
}
// A turned profile (radius by height) as stacked prisms: a baluster, a vase, a barrel.
PolyMesh Turned(float x, float z, const std::vector<std::pair<float, float>>& yr, int sides) {   // (y, radius) steps, each a slab to the next y
    PolyMesh m;
    for (size_t i = 0; i + 1 < yr.size(); ++i) {
        const float r0 = yr[i].second, r1 = yr[i + 1].second;
        const float r = (r0 + r1) * 0.5f;
        Frame f = FrameAA({x - r, yr[i].first, z - r}, {x + r, yr[i + 1].first - 0.0015f, z + r});
        shapes::Append(m, shapes::Prism(f, sides));
    }
    return m;
}

// ---- Furniture -----------------------------------------------------------------------------------------------------
enum Kind { kChair, kTable, kBed, kSofa, kShelf, kDesk, kWardrobe };

void GenerateFurniture(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const int kind = (int)r.Param("kind");
    const float W = Pf(r, "width"), D = Pf(r, "depth"), H = Pf(r, "height"), T = Pf(r, "thickness");
    const std::string legD = r.DesignFor("legs", "leg/0");
    const bool hasLegs = kind != kShelf && kind != kWardrobe && kind != kSofa;
    // vertical positions
    const float seatY = kind == kChair ? H * 0.5f : kind == kSofa ? H * 0.42f : kind == kBed ? H * 0.38f : H;   // the surface you use
    const float topY = kind == kChair || kind == kSofa ? seatY : kind == kBed ? seatY : kind == kTable || kind == kDesk ? H : H;
    const float lh = (kind == kShelf || kind == kWardrobe) ? 0.08f : kind == kSofa ? 0.12f : std::max(0.05f, topY - T);   // leg height
    // legs: one part per corner (a panel design is two end slabs)
    if (hasLegs || kind == kWardrobe || kind == kSofa || kind == kShelf) {
        const float lx = W * 0.5f - T * 0.7f, lz = D * 0.5f - T * 0.7f;
        const float lt = std::max(0.02f, T * 0.9f);
        int idx = 0;
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sz = -1; sz <= 1; sz += 2) {
                const float cx = sx * lx, cz = sz * lz;
                const int i = idx++;
                o.Add("leg/" + std::to_string(i), "legs", FrameAA({cx - lt, 0, cz - lt}, {cx + lt, lh + 0.02f, cz + lt}), [&](const std::string& d) {
                    if (d == "panel") {   // two end slabs instead of four legs
                        if (i >= 2) return PolyMesh();
                        const float x = i == 0 ? -W * 0.5f + T * 0.2f : W * 0.5f - T * 1.3f;
                        return BoxAA({x, 0, -D * 0.5f + T * 0.3f}, {x + T * 1.1f, lh + 0.02f, D * 0.5f - T * 0.3f});
                    }
                    if (d == "round") return Cyl(cx, 0, lh + 0.02f, cz, lt, lt, 10);
                    if (d == "turned") return Turned(cx, cz, {{0, lt * 0.8f}, {lh * 0.12f, lt * 0.8f}, {lh * 0.3f, lt * 1.15f}, {lh * 0.55f, lt * 0.7f}, {lh * 0.8f, lt * 1.0f}, {lh + 0.02f, lt}}, 10);
                    if (d == "splayed") return Prof({{cx - lt - sx * 0.04f, 0}, {cx + lt - sx * 0.04f, 0}, {cx + lt, lh + 0.02f}, {cx - lt, lh + 0.02f}}, cz - lt, cz + lt);
                    return BoxAA({cx - lt, 0, cz - lt}, {cx + lt, lh + 0.02f, cz + lt});
                });
            }
    }
    // the main surface or body
    const float bodyBottom = kind == kShelf || kind == kWardrobe ? 0.1f : lh;
    switch (kind) {
    case kChair: case kTable: case kDesk: case kBed: case kSofa: {
        float y0 = kind == kSofa ? lh : topY - T, y1 = kind == kSofa ? seatY : topY;
        if (kind == kBed) { y0 = lh; y1 = lh + H * 0.14f; }
        Frame f = FrameAA({-W * 0.5f, y0, -D * 0.5f}, {W * 0.5f, y1, D * 0.5f});
        o.Add("top/0", "top", f, [&](const std::string& d) {
            if (d == "round") return Cyl(0, y0, y1, 0, W * 0.5f, D * 0.5f, 24);
            if (d == "lip") {
                PolyMesh m = BoxAA({-W * 0.5f, y0, -D * 0.5f}, {W * 0.5f, y1, D * 0.5f});
                const float lp = T * 0.5f, hh = T * 0.6f;
                shapes::Append(m, BoxAA({-W * 0.5f + 0.003f, y1 - 0.005f, -D * 0.5f + 0.003f}, {W * 0.5f - 0.003f, y1 + hh, -D * 0.5f + lp}));
                shapes::Append(m, BoxAA({-W * 0.5f + 0.003f, y1 - 0.005f, D * 0.5f - lp}, {W * 0.5f - 0.003f, y1 + hh, D * 0.5f - 0.003f}));
                return m;
            }
            if (d == "bevel") return Prof({{-W * 0.5f, y0 + 0.01f}, {-W * 0.5f + 0.02f, y0}, {W * 0.5f - 0.02f, y0}, {W * 0.5f, y0 + 0.01f}, {W * 0.5f, y1 - 0.02f}, {W * 0.5f - 0.03f, y1}, {-W * 0.5f + 0.03f, y1}, {-W * 0.5f, y1 - 0.02f}}, -D * 0.5f, D * 0.5f);
            return BoxAA({-W * 0.5f, y0, -D * 0.5f}, {W * 0.5f, y1, D * 0.5f});
        });
        // rails under a table or desk top tie the legs together
        if ((kind == kTable || kind == kDesk || kind == kChair) && legD != "panel" && legD != "none") {
            const float ry0 = y0 - T * 0.9f, ry1 = y0 - 0.004f;
            const float lx = W * 0.5f - T * 0.7f, lz = D * 0.5f - T * 0.7f;
            o.Add("rail/0", "legs", FrameAA({-W * 0.5f, ry0, -D * 0.5f}, {W * 0.5f, ry1, D * 0.5f}), [&](const std::string& d) {
                if (d == "none") return PolyMesh();
                PolyMesh m;
                shapes::Append(m, BoxAA({-lx, ry0, -lz - 0.004f}, {lx, ry1, -lz + 0.012f}));
                shapes::Append(m, BoxAA({-lx, ry0, lz - 0.012f}, {lx, ry1, lz + 0.004f}));
                return m;
            });
        }
        break; }
    case kShelf: case kWardrobe: {
        const float sx = T * 0.8f;
        o.Add("body/0", "top", FrameAA({-W * 0.5f, bodyBottom, -D * 0.5f}, {W * 0.5f, H, D * 0.5f}), [&](const std::string& d) {
            PolyMesh m;
            shapes::Append(m, BoxAA({-W * 0.5f, bodyBottom, -D * 0.5f}, {-W * 0.5f + sx, H, D * 0.5f}));
            shapes::Append(m, BoxAA({W * 0.5f - sx, bodyBottom, -D * 0.5f}, {W * 0.5f, H, D * 0.5f}));
            shapes::Append(m, BoxAA({-W * 0.5f + sx * 0.5f, bodyBottom, -D * 0.5f - 0.0f}, {W * 0.5f - sx * 0.5f, bodyBottom + sx, D * 0.5f - T * 0.3f}));
            shapes::Append(m, BoxAA({-W * 0.5f + sx * 0.5f, H - sx, -D * 0.5f}, {W * 0.5f - sx * 0.5f, H + (d == "lip" ? 0.03f : 0.0f), D * 0.5f}));
            shapes::Append(m, BoxAA({-W * 0.5f + sx * 0.6f, bodyBottom + sx * 0.5f, -D * 0.5f + 0.003f}, {W * 0.5f - sx * 0.6f, H - sx * 0.5f, -D * 0.5f + T * 0.35f}));   // back panel
            return m;
        });
        if (kind == kShelf) {
            const int n = std::max(1, (int)r.Param("shelves"));
            o.Add("shelf/0", "storage", FrameAA({-W * 0.5f, bodyBottom, -D * 0.5f}, {W * 0.5f, H, D * 0.5f}), [&](const std::string& d) {
                if (d == "none") return PolyMesh();
                PolyMesh m;
                for (int k = 1; k <= n; ++k) {
                    const float y = bodyBottom + (H - bodyBottom) * (float)k / (float)(n + 1);
                    shapes::Append(m, BoxAA({-W * 0.5f + sx * 0.7f, y - sx * 0.4f, -D * 0.5f + T * 0.3f}, {W * 0.5f - sx * 0.7f, y + sx * 0.4f, D * 0.5f - 0.01f}));
                }
                return m;
            });
        }
        if (kind == kWardrobe)
            o.Add("doors/0", "storage", FrameAA({-W * 0.5f, bodyBottom, D * 0.5f - 0.02f}, {W * 0.5f, H, D * 0.5f + 0.03f}), [&](const std::string& d) {
                if (d == "none") return PolyMesh();
                PolyMesh m;
                const int nd = d == "single" ? 1 : 2;
                const float iw = W - sx * 1.2f;
                for (int k = 0; k < nd; ++k) {
                    const float x0 = -iw * 0.5f + iw * (float)k / (float)nd + 0.004f, x1 = -iw * 0.5f + iw * (float)(k + 1) / (float)nd - 0.004f;
                    shapes::Append(m, BoxAA({x0, bodyBottom + sx * 0.4f, D * 0.5f - 0.01f}, {x1, H - sx * 0.4f, D * 0.5f + 0.025f}));
                    const float hx = nd == 1 ? x1 - 0.05f : (k ? x0 + 0.05f : x1 - 0.05f);
                    shapes::Append(m, BoxAA({hx - 0.012f, H * 0.5f - 0.07f, D * 0.5f + 0.02f}, {hx + 0.012f, H * 0.5f + 0.07f, D * 0.5f + 0.045f}));
                }
                return m;
            });
        break; }
    }
    // back (chair back, headboard, sofa back)
    if (kind == kChair || kind == kSofa || kind == kBed) {
        const float by0 = kind == kBed ? lh : kind == kSofa ? seatY : seatY, by1 = kind == kBed ? H : H;
        const float bz1 = -D * 0.5f - 0.004f, bz0 = bz1 + T * (kind == kSofa ? 2.2f : 1.0f);
        o.Add("back/0", "back", FrameAA({-W * 0.5f, by0, bz1}, {W * 0.5f, by1, bz0}), [&](const std::string& d) {
            if (d == "none") return PolyMesh();
            if (d == "slats") {
                PolyMesh m;
                const int n = std::max(3, (int)(W / 0.09f));
                for (int k = 0; k < n; ++k) {
                    const float x = -W * 0.5f + T * 0.5f + (W - T) * ((float)k + 0.5f) / (float)n, w = (W - T) / (float)n * 0.35f;
                    shapes::Append(m, BoxAA({x - w, by0 - 0.01f, bz1}, {x + w, by1 - 0.01f, bz0}));
                }
                shapes::Append(m, BoxAA({-W * 0.5f, by1 - T * 0.9f, bz1 - 0.004f}, {W * 0.5f, by1, bz0 + 0.004f}));
                return m;
            }
            if (d == "rail") {
                PolyMesh m;
                for (int s = -1; s <= 1; s += 2) shapes::Append(m, BoxAA({s * (W * 0.5f - T * 0.7f) - T * 0.5f, by0, bz1}, {s * (W * 0.5f - T * 0.7f) + T * 0.5f, by1, bz0}));
                shapes::Append(m, BoxAA({-W * 0.5f + T * 0.2f, by1 - T * 1.3f, bz1 - 0.004f}, {W * 0.5f - T * 0.2f, by1 - T * 0.2f, bz0 + 0.004f}));
                shapes::Append(m, BoxAA({-W * 0.5f + T * 0.9f, by0 + (by1 - by0) * 0.45f, bz1 + 0.003f}, {W * 0.5f - T * 0.9f, by0 + (by1 - by0) * 0.45f + T * 0.8f, bz0 - 0.003f}));
                return m;
            }
            if (d == "rounded") return Prof({{-W * 0.5f, by0}, {W * 0.5f, by0}, {W * 0.5f, by1 - W * 0.12f}, {W * 0.38f, by1 - 0.01f}, {-W * 0.38f, by1 - 0.01f}, {-W * 0.5f, by1 - W * 0.12f}}, bz1, bz0);
            return BoxAA({-W * 0.5f, by0, bz1}, {W * 0.5f, by1, bz0});   // panel
        });
    }
    // arms (chair, sofa)
    if (kind == kChair || kind == kSofa) {
        const float ay = kind == kSofa ? seatY : seatY, ah = kind == kSofa ? H * 0.3f : H * 0.16f;
        for (int s = 0; s < 2; ++s) {
            const float sg = s ? 1.0f : -1.0f, x0 = sg > 0 ? W * 0.5f - T * 1.1f : -W * 0.5f, x1 = sg > 0 ? W * 0.5f : -W * 0.5f + T * 1.1f;
            const float zb = -D * 0.5f + T * 1.2f;
            o.Add("arm/" + std::to_string(s), "arms", FrameAA({x0, ay, zb}, {x1, ay + ah, D * 0.5f}), [&](const std::string& d) {
                if (d == "none") return PolyMesh();
                if (d == "rolled") return Limb({sg * (W * 0.5f - ah * 0.5f), ay + ah * 0.5f, zb}, {sg * (W * 0.5f - ah * 0.5f), ay + ah * 0.5f, D * 0.5f}, ah * 0.5f, true, 12);
                return BoxAA({x0, ay - 0.004f, zb}, {x1, ay + ah, D * 0.5f - 0.004f});
            });
        }
    }
    // cushion
    if (kind == kChair || kind == kSofa || kind == kBed) {
        const float cy0 = kind == kBed ? lh + H * 0.14f : seatY, ch = kind == kBed ? H * 0.12f : kind == kSofa ? H * 0.16f : T * 1.2f;
        const float cx = W * 0.5f - (kind == kSofa ? T * 1.6f : T * 0.8f), cz0 = -D * 0.5f + (kind == kBed ? 0.08f : T * 2.4f), cz1 = D * 0.5f - T * 0.5f;
        o.Add("cushion/0", "cushion", FrameAA({-cx, cy0, cz0}, {cx, cy0 + ch, cz1}), [&](const std::string& d) {
            if (d == "none") return PolyMesh();
            if (d == "puffy") return Ell({0, cy0 + ch * 0.45f, (cz0 + cz1) * 0.5f}, cx, ch * 0.62f, (cz1 - cz0) * 0.5f, 5, 12);
            return BoxAA({-cx, cy0, cz0}, {cx, cy0 + ch, cz1});
        });
    }
    // drawers under a desk
    if (kind == kDesk)
        o.Add("drawers/0", "storage", FrameAA({W * 0.5f - T * 4.5f, lh * 0.2f, -D * 0.5f}, {W * 0.5f - T * 0.9f, lh - 0.01f, D * 0.5f}), [&](const std::string& d) {
            if (d == "none") return PolyMesh();
            PolyMesh m;
            const float x0 = W * 0.5f - 0.3f, x1 = W * 0.5f - T * 1.1f, y0 = std::max(0.08f, lh * 0.2f), y1 = lh - 0.02f;
            shapes::Append(m, BoxAA({x0, y0, -D * 0.5f + 0.04f}, {x1, y1, D * 0.5f - 0.04f}));
            const int n = d == "tall" ? 4 : 3;
            for (int k = 0; k < n; ++k) {
                const float a = y0 + (y1 - y0) * (float)k / (float)n + 0.01f, b = y0 + (y1 - y0) * (float)(k + 1) / (float)n - 0.01f;
                shapes::Append(m, BoxAA({x0 + 0.01f, a, D * 0.5f - 0.045f}, {x1 - 0.01f, b, D * 0.5f - 0.02f}));
            }
            return m;
        });
}

Template MakeFurniture() {
    B b;
    b.t.id = "furniture"; b.t.name = "Furniture"; b.t.category = "Props"; b.t.version = 1;
    b.t.description = "Furniture: chair, table, bed, sofa, bookshelf, desk and wardrobe, each made of legs, a top, a back, arms, cushions and storage you can change.";
    { ParamDef p; p.id = "kind"; p.label = "Kind"; p.kind = ParamDef::Kind::Choice; p.def = 0; p.min = 0; p.max = 6; p.group = "Size"; p.tip = "What it is."; p.choices = {"Chair", "Table", "Bed", "Sofa", "Bookshelf", "Desk", "Wardrobe"}; b.t.params.push_back(p); }
    b.P("width", "Width", 0.5, 0.25, 3.0, "Size", "Left to right.");
    b.P("depth", "Depth", 0.5, 0.2, 2.4, "Size", "Front to back.");
    b.P("height", "Height", 0.9, 0.3, 2.4, "Size", "Overall height.");
    b.P("thickness", "Board thickness", 0.04, 0.015, 0.1, "Size", "Thickness of boards and legs.");
    b.P("shelves", "Shelves", 3, 1, 8, "Size", "How many shelves (bookshelf).", B::K::Int);
    const Color kWood{0.55f, 0.38f, 0.22f, 1}, kDark{0.3f, 0.2f, 0.12f, 1}, kCloth{0.62f, 0.2f, 0.2f, 1}, kLight{0.85f, 0.78f, 0.62f, 1}, kBlue{0.25f, 0.35f, 0.6f, 1}, kWhite{0.9f, 0.9f, 0.88f, 1};
    b.S("legs", "Legs", {{"square", "Square"}, {"round", "Round"}, {"turned", "Turned"}, {"splayed", "Splayed"}, {"panel", "End panels"}, {"none", "None"}}, kWood, 0);
    b.S("top", "Top / body", {{"plain", "Plain"}, {"round", "Round"}, {"lip", "Raised edge"}, {"bevel", "Bevelled"}}, kWood, 1);
    b.S("back", "Back", {{"panel", "Panel"}, {"slats", "Slats"}, {"rail", "Rails"}, {"rounded", "Rounded"}, {"none", "None"}}, kWood, 2);
    b.S("arms", "Arms", {{"flat", "Flat"}, {"rolled", "Rolled"}, {"none", "None"}}, kWood, 3);
    b.S("cushion", "Cushion", {{"flat", "Flat"}, {"puffy", "Puffy"}, {"none", "None"}}, kCloth, 4);
    b.S("storage", "Storage", {{"none", "None"}, {"drawers", "Drawers"}, {"tall", "Tall drawers"}, {"doors", "Two doors"}, {"single", "One door"}}, kDark, 5);
    // shelf: storage design "none" means a plain frame, anything else adds the shelves; the shelf slot uses the same list
    b.St("chair", "Dining chair", "Four legs, a seat and a slatted back.", {{"kind", 0}, {"width", 0.45}, {"depth", 0.45}, {"height", 0.9}}, {{"legs", "square"}, {"back", "slats"}, {"arms", "none"}, {"cushion", "none"}, {"top", "plain"}}, {});
    b.St("table", "Dining table", "A plain top on turned legs with rails.", {{"kind", 1}, {"width", 1.4}, {"depth", 0.8}, {"height", 0.75}, {"thickness", 0.05}}, {{"legs", "turned"}, {"top", "lip"}, {"back", "none"}, {"arms", "none"}, {"cushion", "none"}}, {});
    b.St("round_table", "Round table", "A round top on round legs.", {{"kind", 1}, {"width", 1.0}, {"depth", 1.0}, {"height", 0.72}}, {{"legs", "round"}, {"top", "round"}, {"back", "none"}, {"arms", "none"}, {"cushion", "none"}}, {{"top", Color{0.85f, 0.8f, 0.7f, 1}}});
    b.St("armchair", "Armchair", "A padded seat with arms and a rounded back.", {{"kind", 0}, {"width", 0.8}, {"depth", 0.8}, {"height", 0.95}, {"thickness", 0.06}}, {{"legs", "splayed"}, {"back", "rounded"}, {"arms", "flat"}, {"cushion", "puffy"}, {"top", "bevel"}}, {{"cushion", kBlue}});
    b.St("sofa", "Sofa", "A wide seat, back, arms and cushions.", {{"kind", 3}, {"width", 2.0}, {"depth", 0.9}, {"height", 0.85}, {"thickness", 0.07}}, {{"legs", "round"}, {"back", "panel"}, {"arms", "flat"}, {"cushion", "puffy"}, {"top", "plain"}}, {{"top", kCloth}, {"back", kCloth}, {"arms", kCloth}, {"cushion", kCloth}});
    b.St("bed", "Bed", "A frame, a mattress and a headboard.", {{"kind", 2}, {"width", 1.5}, {"depth", 2.0}, {"height", 1.0}, {"thickness", 0.06}}, {{"legs", "square"}, {"back", "slats"}, {"arms", "none"}, {"cushion", "flat"}, {"top", "plain"}}, {{"cushion", kWhite}});
    b.St("bookshelf", "Bookshelf", "Sides, a back and four shelves.", {{"kind", 4}, {"width", 0.9}, {"depth", 0.3}, {"height", 1.8}, {"shelves", 4}}, {{"legs", "none"}, {"storage", "drawers"}, {"top", "plain"}, {"back", "none"}, {"arms", "none"}, {"cushion", "none"}}, {});
    b.St("desk", "Desk", "A desk top with a drawer stack.", {{"kind", 5}, {"width", 1.3}, {"depth", 0.65}, {"height", 0.76}}, {{"legs", "square"}, {"top", "bevel"}, {"storage", "tall"}, {"back", "none"}, {"arms", "none"}, {"cushion", "none"}}, {});
    b.St("wardrobe", "Wardrobe", "A tall cupboard with two doors.", {{"kind", 6}, {"width", 1.1}, {"depth", 0.55}, {"height", 2.0}}, {{"legs", "none"}, {"storage", "doors"}, {"top", "lip"}, {"back", "none"}, {"arms", "none"}, {"cushion", "none"}}, {});
    (void)kLight;
    b.t.generate = GenerateFurniture;
    return b.t;
}

// ---- Containers ----------------------------------------------------------------------------------------------------
void GenerateContainer(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float W = Pf(r, "width"), D = Pf(r, "depth"), H = Pf(r, "height");
    const std::string body = r.DesignFor("body", "body/0");
    const bool round = body == "barrel" || body == "bucket" || body == "jar";
    const float rx = W * 0.5f, rz = D * 0.5f;
    const float lidH = H * 0.1f;
    float topY = H;
    o.Add("body/0", "body", FrameAA({-rx, 0, -rz}, {rx, H, rz}), [&](const std::string& d) {
        if (d == "barrel") return Turned(0, 0, {{0, rx * 0.8f}, {H * 0.2f, rx * 0.94f}, {H * 0.5f, rx}, {H * 0.8f, rx * 0.94f}, {H, rx * 0.8f}}, 14);
        if (d == "bucket") return Prof({{-rx * 0.8f, 0}, {rx * 0.8f, 0}, {rx, H}, {-rx, H}}, -rz * 0.8f, rz * 0.8f);   // tapered open box shape
        if (d == "jar") return Turned(0, 0, {{0, rx * 0.5f}, {H * 0.15f, rx * 0.85f}, {H * 0.5f, rx}, {H * 0.8f, rx * 0.8f}, {H * 0.92f, rx * 0.5f}, {H, rx * 0.55f}}, 14);
        if (d == "sack") return Ell({0, H * 0.45f, 0}, rx, H * 0.45f, rz, 6, 12);
        if (d == "chest") {
            PolyMesh m = BoxAA({-rx, 0, -rz}, {rx, H * 0.62f, rz});
            shapes::Append(m, Prof({{-rx * 0.98f, H * 0.62f + 0.004f}, {rx * 0.98f, H * 0.62f + 0.004f}, {rx * 0.9f, H * 0.82f}, {0, H}, {-rx * 0.9f, H * 0.82f}}, -rz * 0.98f, rz * 0.98f));
            return m;
        }
        return BoxAA({-rx, 0, -rz}, {rx, H, rz});   // crate
    });
    if (body == "chest") topY = H;
    // lid
    if (body != "sack" && body != "bucket" && body != "chest")
        o.Add("lid/0", "lid", FrameAA({-rx, topY, -rz}, {rx, topY + lidH, rz}), [&](const std::string& d) {
            if (d == "none") return PolyMesh();
            const float lx = round ? rx * (body == "jar" ? 0.6f : 0.82f) : rx * 1.02f, lz = round ? rz * (body == "jar" ? 0.6f : 0.82f) : rz * 1.02f;
            if (d == "domed") return Ell({0, topY + 0.004f, 0}, lx, lidH * 1.2f, lz, 4, 12);
            if (round) return Cyl(0, topY + 0.004f, topY + lidH, 0, lx, lz, 14);
            return BoxAA({-lx, topY + 0.004f, -lz}, {lx, topY + lidH, lz});
        });
    // bands
    o.Add("bands/0", "bands", FrameAA({-rx * 1.05f, 0, -rz * 1.05f}, {rx * 1.05f, H, rz * 1.05f}), [&](const std::string& d) {
        if (d == "none") return PolyMesh();
        PolyMesh m;
        const float t = d == "rope" ? 0.012f : 0.02f;
        const std::vector<float> ys = body == "chest" ? std::vector<float>{H * 0.2f, H * 0.5f} : std::vector<float>{H * 0.2f, H * 0.75f};
        for (float y : ys) {
            if (round) { const float k = body == "barrel" ? (std::fabs(y - H * 0.5f) < H * 0.2f ? 1.04f : 0.97f) : 1.04f; shapes::Append(m, Cyl(0, y + 0.0031f, y + H * 0.05f + 0.0027f, 0, rx * k * 1.003f, rz * k * 1.003f, 14)); }
            else shapes::Append(m, BoxAA({-rx - t, y, -rz - t}, {rx + t, y + H * 0.06f, rz + t}));
        }
        return m;
    });
    // handles
    o.Add("handle/0", "handle", FrameAA({-rx * 1.1f, H * 0.5f, -rz * 0.3f}, {rx * 1.1f, H * 0.9f, rz * 0.3f}), [&](const std::string& d) {
        if (d == "none") return PolyMesh();
        PolyMesh m;
        if (d == "loop") {   // an arc over the top (a bucket or pail)
            for (int s = -1; s <= 1; s += 2) shapes::Append(m, Limb({s * rx * 0.95f, H * 0.95f, 0.0f}, {s * rx * 0.55f, H * 1.28f, 0.0f}, 0.012f, true, 6));
            shapes::Append(m, Limb({-rx * 0.55f, H * 1.28f + 0.003f, 0}, {rx * 0.55f, H * 1.28f + 0.003f, 0}, 0.012f, true, 6));
            return m;
        }
        for (int s = -1; s <= 1; s += 2) shapes::Append(m, BoxAA({s * (rx + 0.02f) - 0.02f + (s > 0 ? 0.0f : 0.0f), H * 0.55f, -rz * 0.2f}, {s * (rx + 0.02f) + 0.02f, H * 0.65f, rz * 0.2f}));
        return m;
    });
}

Template MakeContainer() {
    B b;
    b.t.id = "container"; b.t.name = "Container"; b.t.category = "Props"; b.t.version = 1;
    b.t.description = "A container: crate, barrel, chest, bucket, jar or sack, with a lid, metal bands and handles you can change.";
    b.P("width", "Width", 0.6, 0.15, 2.0, "Size", "Left to right.");
    b.P("depth", "Depth", 0.6, 0.15, 2.0, "Size", "Front to back.");
    b.P("height", "Height", 0.6, 0.15, 2.0, "Size", "Overall height.");
    const Color kWood{0.55f, 0.38f, 0.22f, 1}, kMetal{0.35f, 0.36f, 0.4f, 1}, kClay{0.7f, 0.42f, 0.28f, 1}, kCloth{0.75f, 0.68f, 0.5f, 1};
    b.S("body", "Body", {{"crate", "Crate"}, {"barrel", "Barrel"}, {"chest", "Chest"}, {"bucket", "Bucket"}, {"jar", "Jar"}, {"sack", "Sack"}}, kWood, 0);
    b.S("lid", "Lid", {{"flat", "Flat"}, {"domed", "Domed"}, {"none", "None"}}, kWood, 1);
    b.S("bands", "Bands", {{"none", "None"}, {"metal", "Metal"}, {"rope", "Rope"}}, kMetal, 2);
    b.S("handle", "Handles", {{"none", "None"}, {"loop", "Loop"}, {"side", "Side grips"}}, kMetal, 3);
    b.St("crate", "Wooden crate", "A box with a flat lid.", {}, {{"body", "crate"}, {"lid", "flat"}, {"bands", "none"}, {"handle", "side"}}, {});
    b.St("barrel", "Barrel", "A barrel with metal bands.", {{"width", 0.6}, {"depth", 0.6}, {"height", 0.9}}, {{"body", "barrel"}, {"lid", "flat"}, {"bands", "metal"}, {"handle", "none"}}, {});
    b.St("chest", "Treasure chest", "A domed chest with bands.", {{"width", 0.9}, {"depth", 0.55}, {"height", 0.55}}, {{"body", "chest"}, {"lid", "none"}, {"bands", "metal"}, {"handle", "side"}}, {});
    b.St("bucket", "Bucket", "A tapered pail with a loop handle.", {{"width", 0.4}, {"depth", 0.4}, {"height", 0.35}}, {{"body", "bucket"}, {"lid", "none"}, {"bands", "none"}, {"handle", "loop"}}, {{"body", kMetal}});
    b.St("jar", "Clay jar", "A round jar with a small lid.", {{"width", 0.4}, {"depth", 0.4}, {"height", 0.55}}, {{"body", "jar"}, {"lid", "domed"}, {"bands", "none"}, {"handle", "none"}}, {{"body", kClay}, {"lid", kClay}});
    b.St("sack", "Grain sack", "A soft sack.", {{"width", 0.5}, {"depth", 0.4}, {"height", 0.7}}, {{"body", "sack"}, {"lid", "none"}, {"bands", "rope"}, {"handle", "none"}}, {{"body", kCloth}, {"bands", Color{0.4f, 0.3f, 0.18f, 1}}});
    b.t.generate = GenerateContainer;
    return b.t;
}

// ---- Tools ---------------------------------------------------------------------------------------------------------
void GenerateTool(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float L = Pf(r, "length"), hs = Pf(r, "headSize"), hr = Pf(r, "handleRadius");
    const std::string headD = r.DesignFor("head", "head/0");
    const float handleTop = headD == "blade" ? L * 0.28f : L;
    o.Add("handle/0", "handle", FrameAA({-hr, 0, -hr}, {hr, handleTop, hr}), [&](const std::string& d) {
        if (d == "square") return BoxAA({-hr, 0, -hr}, {hr, handleTop, hr});
        if (d == "curved") { PolyMesh m; const int n = 6; for (int k = 0; k < n; ++k) { const float y0 = handleTop * (float)k / n, y1 = handleTop * (float)(k + 1) / n; const float x0 = 0.03f * std::sin(3.1416f * (float)k / n), x1 = 0.03f * std::sin(3.1416f * (float)(k + 1) / n); shapes::Append(m, Limb({x0, y0 - (k ? 0.004f : 0), 0}, {x1, y1, 0}, hr, true, 8)); } return m; }
        if (d == "wrapped") { PolyMesh m = Cyl(0, 0, handleTop, 0, hr, hr, 8); for (int k = 0; k < 5; ++k) shapes::Append(m, Cyl(0, handleTop * (0.05f + 0.07f * (float)k), handleTop * (0.05f + 0.07f * (float)k) + 0.018f, 0, hr * 1.25f, hr * 1.25f, 8)); return m; }
        return Cyl(0, 0, handleTop, 0, hr, hr, 10);
    });
    const float t = L;
    o.Add("head/0", "head", FrameAA({-hs, t - hs * 0.5f, -hr * 1.4f}, {hs, t + hs * 0.9f, hr * 1.4f}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "hammer") { shapes::Append(m, BoxAA({-hs * 0.6f, t - hs * 0.3f, -hr * 1.8f}, {hs * 0.6f, t + hs * 0.4f, hr * 1.8f})); shapes::Append(m, BoxAA({hs * 0.6f + 0.004f, t - hs * 0.2f, -hr * 1.2f}, {hs * 1.2f, t + hs * 0.3f, hr * 1.2f})); return m; }
        if (d == "axe") { shapes::Append(m, BoxAA({-hr * 1.5f, t - hs * 0.4f, -hr * 1.3f}, {hr * 1.5f, t + hs * 0.1f, hr * 1.3f})); shapes::Append(m, Prof({{hr * 1.4f, t - hs * 0.9f}, {hs * 1.4f, t - hs * 1.1f}, {hs * 1.4f, t + hs * 0.6f}, {hr * 1.4f, t + hs * 0.3f}}, -hr * 0.45f, hr * 0.45f)); return m; }
        if (d == "pickaxe") { shapes::Append(m, BoxAA({-hr * 1.5f, t - hs * 0.2f, -hr * 1.3f}, {hr * 1.5f, t + hs * 0.2f, hr * 1.3f})); for (int s = -1; s <= 1; s += 2) shapes::Append(m, Prof({{s * hr * 1.4f, t + hs * 0.2f}, {s * hr * 1.4f, t - hs * 0.2f}, {s * hs * 1.7f, t - hs * 0.55f}}, -hr * 0.55f, hr * 0.55f)); return m; }
        if (d == "shovel") return Prof({{-hs * 0.6f, t - hs * 1.6f}, {hs * 0.6f, t - hs * 1.6f}, {hs * 0.55f, t - hs * 0.4f}, {hr * 1.2f, t + hs * 0.05f}, {-hr * 1.2f, t + hs * 0.05f}, {-hs * 0.55f, t - hs * 0.4f}}, -hr * 0.3f, hr * 0.3f);
        if (d == "wrench") { m = BoxAA({-hr * 1.2f, t - hs * 0.3f, -hr * 0.6f}, {hr * 1.2f, t + hs * 0.2f, hr * 0.6f}); for (int s = -1; s <= 1; s += 2) shapes::Append(m, BoxAA({s * hs * 0.42f - hs * 0.13f, t + hs * 0.2f, -hr * 0.6f}, {s * hs * 0.42f + hs * 0.13f, t + hs * 0.95f, hr * 0.6f})); return m; }
        if (d == "blade") return Prof({{-hs * 0.28f, handleTop - 0.02f}, {hs * 0.28f, handleTop - 0.02f}, {hs * 0.24f, t - hs * 0.25f}, {0, t + hs * 0.05f}, {-hs * 0.24f, t - hs * 0.25f}}, -hr * 0.28f, hr * 0.28f);
        // sickle / default hoe
        return Prof({{-hr, t - hs * 0.2f}, {hr, t - hs * 0.2f}, {hs * 1.1f, t - hs * 0.5f}, {hs * 1.1f, t - hs * 0.2f}, {hr, t + hs * 0.2f}, {-hr, t + hs * 0.2f}}, -hr * 0.4f, hr * 0.4f);
    });
    if (headD == "blade")   // a hand guard and a pommel for a sword
        o.Add("guard/0", "guard", FrameAA({-hs * 0.5f, handleTop - 0.02f, -hr * 1.2f}, {hs * 0.5f, handleTop + 0.05f, hr * 1.2f}), [&](const std::string& d) {
            if (d == "none") return PolyMesh();
            PolyMesh m = BoxAA({-hs * 0.5f, handleTop - 0.015f, -hr * 1.1f}, {hs * 0.5f, handleTop + 0.045f, hr * 1.1f});
            if (d == "curved") for (int s = -1; s <= 1; s += 2) shapes::Append(m, Prof({{s * hs * 0.5f, handleTop - 0.012f}, {s * hs * 0.62f, handleTop + 0.06f}, {s * hs * 0.5f, handleTop + 0.04f}}, -hr * 0.9f, hr * 0.9f));
            return m;
        });
    o.Add("end/0", "end", FrameAA({-hr * 1.6f, -0.05f, -hr * 1.6f}, {hr * 1.6f, 0.06f, hr * 1.6f}), [&](const std::string& d) {
        if (d == "none") return PolyMesh();
        if (d == "hook") return Limb({0, 0.01f, 0}, {hr * 2.2f, -0.04f, 0}, hr * 0.5f, true, 6);
        return Ell({0, 0.01f, 0}, hr * 1.5f, hr * 1.4f, hr * 1.5f, 4, 8);
    });
}

Template MakeTool() {
    B b;
    b.t.id = "tool"; b.t.name = "Tool"; b.t.category = "Props"; b.t.version = 1;
    b.t.description = "A hand tool or weapon standing upright: a handle, a head (hammer, axe, pickaxe, shovel, wrench, sword) and an end cap.";
    b.P("length", "Length", 0.9, 0.2, 2.2, "Size", "Length of the handle.");
    b.P("headSize", "Head size", 0.14, 0.05, 0.4, "Size", "Size of the head or blade.");
    b.P("handleRadius", "Handle radius", 0.018, 0.008, 0.05, "Size", "Thickness of the handle.");
    const Color kWood{0.55f, 0.38f, 0.22f, 1}, kSteel{0.62f, 0.65f, 0.7f, 1}, kLeather{0.35f, 0.22f, 0.14f, 1};
    b.S("handle", "Handle", {{"round", "Round"}, {"square", "Square"}, {"curved", "Curved"}, {"wrapped", "Wrapped"}}, kWood, 0);
    b.S("head", "Head", {{"hammer", "Hammer"}, {"axe", "Axe"}, {"pickaxe", "Pickaxe"}, {"shovel", "Shovel"}, {"wrench", "Wrench"}, {"blade", "Sword blade"}, {"hoe", "Hoe"}}, kSteel, 1);
    b.S("guard", "Hand guard", {{"straight", "Straight"}, {"curved", "Curved"}, {"none", "None"}}, kSteel, 2);
    b.S("end", "End", {{"none", "None"}, {"cap", "Cap"}, {"hook", "Hook"}}, kLeather, 3);
    b.St("hammer", "Hammer", "A wooden handle and a steel head.", {{"length", 0.45}, {"headSize", 0.1}}, {{"handle", "round"}, {"head", "hammer"}, {"end", "cap"}}, {});
    b.St("axe", "Axe", "A long handle and a steel blade.", {{"length", 0.8}, {"headSize", 0.16}}, {{"handle", "curved"}, {"head", "axe"}, {"end", "none"}}, {});
    b.St("pickaxe", "Pickaxe", "Two points on a long handle.", {{"length", 0.95}, {"headSize", 0.2}}, {{"handle", "square"}, {"head", "pickaxe"}, {"end", "none"}}, {});
    b.St("shovel", "Shovel", "A scoop on a long handle.", {{"length", 1.2}, {"headSize", 0.17}}, {{"handle", "round"}, {"head", "shovel"}, {"end", "hook"}}, {});
    b.St("wrench", "Wrench", "A steel wrench with a wrapped grip.", {{"length", 0.3}, {"headSize", 0.1}, {"handleRadius", 0.014}}, {{"handle", "wrapped"}, {"head", "wrench"}, {"end", "cap"}}, {});
    b.St("sword", "Sword", "A blade, a guard and a wrapped grip.", {{"length", 1.0}, {"headSize", 0.2}, {"handleRadius", 0.014}}, {{"handle", "wrapped"}, {"head", "blade"}, {"guard", "straight"}, {"end", "cap"}}, {{"handle", kLeather}});
    b.t.generate = GenerateTool;
    return b.t;
}

// ---- Decorations ---------------------------------------------------------------------------------------------------
void GenerateDecoration(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float H = Pf(r, "height"), W = Pf(r, "width"), pr = Pf(r, "postRadius");
    const std::string baseD = r.DesignFor("base", "base/0"), topD = r.DesignFor("top", "top/0");
    const float bh = baseD == "steps" ? 0.24f : baseD == "none" ? 0.0f : 0.12f;
    o.Add("base/0", "base", FrameAA({-W * 0.5f, 0, -W * 0.5f}, {W * 0.5f, bh, W * 0.5f}), [&](const std::string& d) {
        if (d == "none") return PolyMesh();
        if (d == "round") return Cyl(0, 0, bh, 0, W * 0.5f, W * 0.5f, 16);
        if (d == "steps") { PolyMesh m = BoxAA({-W * 0.5f, 0, -W * 0.5f}, {W * 0.5f, bh * 0.5f, W * 0.5f}); shapes::Append(m, BoxAA({-W * 0.38f, bh * 0.5f - 0.003f, -W * 0.38f}, {W * 0.38f, bh, W * 0.38f})); return m; }
        return BoxAA({-W * 0.5f, 0, -W * 0.5f}, {W * 0.5f, bh, W * 0.5f});
    });
    const float sy0 = std::max(0.0f, bh - 0.01f), sy1 = topD == "fountain" ? bh + H * 0.35f : H * 0.78f;
    o.Add("shaft/0", "shaft", FrameAA({-pr * 2, sy0, -pr * 2}, {pr * 2, sy1, pr * 2}), [&](const std::string& d) {
        if (d == "none") return PolyMesh();
        if (d == "column") { PolyMesh m = Cyl(0, sy0, sy1, 0, pr * 1.6f, pr * 1.6f, 14); shapes::Append(m, Cyl(0, sy0 + 0.002f, sy0 + 0.08f, 0, pr * 2.1f, pr * 2.1f, 14)); shapes::Append(m, Cyl(0, sy1 - 0.08f, sy1 + 0.04f, 0, pr * 2.1f, pr * 2.1f, 14)); return m; }
        if (d == "pole") return Cyl(0, sy0, sy1, 0, pr * 0.7f, pr * 0.7f, 8);
        if (d == "twisted") { PolyMesh m; const int n = 8; for (int k = 0; k < n; ++k) { const float y0 = sy0 + (sy1 - sy0) * (float)k / n, y1 = sy0 + (sy1 - sy0) * (float)(k + 1) / n; const float a = 0.5f * (float)k; shapes::Append(m, Limb({std::cos(a) * pr * 0.6f, y0 - (k ? 0.004f : 0), std::sin(a) * pr * 0.6f}, {std::cos(a + 0.5f) * pr * 0.6f, y1, std::sin(a + 0.5f) * pr * 0.6f}, pr, true, 6)); } return m; }
        return BoxAA({-pr, sy0, -pr}, {pr, sy1, pr});   // post
    });
    const float ty = sy1;
    o.Add("top/0", "top", FrameAA({-W * 0.5f, ty - 0.1f, -W * 0.5f}, {W * 0.5f, H + 0.1f, W * 0.5f}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "sign") { shapes::Append(m, BoxAA({-W * 0.5f, ty - W * 0.18f, -0.02f}, {W * 0.5f, ty + W * 0.22f, 0.02f})); shapes::Append(m, BoxAA({-W * 0.5f - 0.015f, ty - W * 0.2f, -0.03f}, {-W * 0.5f + 0.02f, ty + W * 0.24f, 0.03f})); shapes::Append(m, BoxAA({W * 0.5f - 0.02f, ty - W * 0.2f, -0.03f}, {W * 0.5f + 0.015f, ty + W * 0.24f, 0.03f})); return m; }
        if (d == "arrow") { return Prof({{-W * 0.5f, ty - W * 0.08f}, {W * 0.25f, ty - W * 0.08f}, {W * 0.25f, ty - W * 0.18f}, {W * 0.55f, ty}, {W * 0.25f, ty + W * 0.18f}, {W * 0.25f, ty + W * 0.08f}, {-W * 0.5f, ty + W * 0.08f}}, -0.02f, 0.02f); }
        if (d == "flag") { m = Prof({{0.01f, H * 0.95f}, {W * 0.9f, H * 0.9f}, {W * 0.85f, H * 0.72f}, {0.01f, H * 0.7f}}, -0.008f, 0.008f); shapes::Append(m, Ell({0, H + 0.02f, 0}, pr * 1.2f, pr * 1.2f, pr * 1.2f, 4, 8)); return m; }
        if (d == "orb") return Ell({0, ty + W * 0.3f, 0}, W * 0.3f, W * 0.3f, W * 0.3f, 6, 12);
        if (d == "bust") { m = Ell({0, ty + W * 0.12f, 0}, W * 0.3f, W * 0.22f, W * 0.18f, 5, 10); shapes::Append(m, Ell({0, ty + W * 0.5f, 0.0f}, W * 0.19f, W * 0.24f, W * 0.2f, 5, 10)); return m; }
        if (d == "lantern") { m = BoxAA({-W * 0.22f, ty, -W * 0.22f}, {W * 0.22f, ty + W * 0.5f, W * 0.22f}); shapes::Append(m, Cone({0, ty + W * 0.5f, 0}, {0, ty + W * 0.8f, 0}, W * 0.32f, 4)); return m; }
        if (d == "bowl") { m = Turned(0, 0, {{ty, W * 0.2f}, {ty + 0.06f, W * 0.34f}, {ty + 0.16f, W * 0.5f}}, 16); shapes::Append(m, Cyl(0, ty + 0.2f, ty + 0.205f, 0, W * 0.43f, W * 0.43f, 16)); return m; }
        return PolyMesh();
    });
}

Template MakeDecoration() {
    B b;
    b.t.id = "decoration"; b.t.name = "Decoration"; b.t.category = "Props"; b.t.version = 1;
    b.t.description = "A decoration: a base, a shaft and a top (sign, arrow, flag, orb, bust, lantern or fountain bowl).";
    b.P("height", "Height", 2.0, 0.4, 5, "Size", "Overall height.");
    b.P("width", "Width", 0.9, 0.2, 3, "Size", "Width of the base and the top.");
    b.P("postRadius", "Post radius", 0.05, 0.02, 0.3, "Size", "Thickness of the shaft.");
    const Color kWood{0.55f, 0.38f, 0.22f, 1}, kStone{0.62f, 0.62f, 0.6f, 1}, kRed{0.75f, 0.15f, 0.15f, 1}, kIron{0.15f, 0.16f, 0.17f, 1}, kLight{0.98f, 0.9f, 0.6f, 1}, kWater{0.45f, 0.65f, 0.85f, 1};
    b.S("base", "Base", {{"block", "Block"}, {"round", "Round"}, {"steps", "Steps"}, {"none", "None"}}, kStone, 0);
    b.S("shaft", "Shaft", {{"post", "Post"}, {"column", "Column"}, {"pole", "Pole"}, {"twisted", "Twisted"}, {"none", "None"}}, kWood, 1);
    b.S("top", "Top", {{"sign", "Sign board"}, {"arrow", "Arrow sign"}, {"flag", "Flag"}, {"orb", "Orb"}, {"bust", "Bust"}, {"lantern", "Lantern"}, {"bowl", "Bowl"}}, kRed, 2);
    b.St("signpost", "Signpost", "A wooden post with a board.", {{"height", 1.8}, {"width", 0.8}}, {{"base", "none"}, {"shaft", "post"}, {"top", "sign"}}, {});
    b.St("arrow_sign", "Direction sign", "A post with an arrow.", {{"height", 1.7}, {"width", 0.9}}, {{"base", "none"}, {"shaft", "post"}, {"top", "arrow"}}, {{"top", Color{0.85f, 0.8f, 0.55f, 1}}});
    b.St("flagpole", "Flagpole", "A tall pole and a flag.", {{"height", 4.0}, {"width", 1.0}, {"postRadius", 0.04}}, {{"base", "round"}, {"shaft", "pole"}, {"top", "flag"}}, {{"shaft", kIron}});
    b.St("statue", "Statue", "A bust on a column and steps.", {{"height", 2.4}, {"width", 1.0}, {"postRadius", 0.16}}, {{"base", "steps"}, {"shaft", "column"}, {"top", "bust"}}, {{"top", kStone}, {"shaft", kStone}});
    b.St("garden_lantern", "Garden lantern", "A stone post with a lantern.", {{"height", 1.4}, {"width", 0.6}, {"postRadius", 0.07}}, {{"base", "block"}, {"shaft", "post"}, {"top", "lantern"}}, {{"shaft", kStone}, {"top", kLight}});
    b.St("fountain", "Fountain", "A round base, a short column and a bowl.", {{"height", 1.4}, {"width", 1.6}, {"postRadius", 0.1}}, {{"base", "round"}, {"shaft", "column"}, {"top", "bowl"}}, {{"top", kStone}, {"shaft", kStone}});
    b.t.generate = GenerateDecoration;
    return b.t;
}

} // namespace

void RegisterPropTemplates() {
    Registry& r = Registry::Instance();
    r.Register(MakeFurniture());
    r.Register(MakeContainer());
    r.Register(MakeTool());
    r.Register(MakeDecoration());
}

} // namespace okay::modeling::guided
