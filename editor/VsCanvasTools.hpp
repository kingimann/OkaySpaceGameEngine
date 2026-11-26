#pragma once
// Layout helpers for the Visual Script editor: snap, alignment, and the
// variables / parameters the side panel lists. Pure data, no ImGui, so the
// editor self-test can cover them. Nothing here is written into a .okayvs file.
#include "okay/VisualScript/VsTypes.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace okay::editor::vsed {

/// Round a layout coordinate to the nearest multiple of `grid`. A non-positive
/// grid, or a NaN coordinate, is left alone (saved graphs keep their positions
/// until the user actually moves a node).
inline float VsSnap(float v, float grid) {
    if (!(grid > 0.0f) || !(v == v)) return v;
    return std::round(v / grid) * grid;
}

enum class VsAlign { Left, Right, Top, Bottom, CenterX, CenterY };

/// Line up node top-left positions. Right and bottom use each node's width and
/// height so the far edges meet; left and top share the minimum origin.
inline void VsAlignNodes(std::vector<float>& xs, std::vector<float>& ys,
                         const std::vector<float>& widths, const std::vector<float>& heights,
                         VsAlign how) {
    if (xs.empty() || xs.size() != ys.size()) return;
    float x0 = xs[0], y0 = ys[0], x1 = xs[0], y1 = ys[0];
    for (std::size_t i = 0; i < xs.size(); ++i) {
        const float w = i < widths.size() ? widths[i] : 0.0f;
        const float h = i < heights.size() ? heights[i] : 0.0f;
        x0 = std::min(x0, xs[i]);
        y0 = std::min(y0, ys[i]);
        x1 = std::max(x1, xs[i] + w);
        y1 = std::max(y1, ys[i] + h);
    }
    for (std::size_t i = 0; i < xs.size(); ++i) {
        const float w = i < widths.size() ? widths[i] : 0.0f;
        const float h = i < heights.size() ? heights[i] : 0.0f;
        switch (how) {
            case VsAlign::Left:   xs[i] = x0; break;
            case VsAlign::Right:  xs[i] = x1 - w; break;
            case VsAlign::Top:    ys[i] = y0; break;
            case VsAlign::Bottom: ys[i] = y1 - h; break;
            case VsAlign::CenterX: xs[i] = (x0 + x1) * 0.5f - w * 0.5f; break;   // middles on one vertical line
            case VsAlign::CenterY: ys[i] = (y0 + y1) * 0.5f - h * 0.5f; break;   // middles on one horizontal line
        }
    }
}

/// Spread nodes evenly: the first and last (by position along the axis) stay, and the gaps between neighbours'
/// edges become equal. `horizontal` spreads along x, else along y. Fewer than three nodes are left alone.
inline void VsDistributeNodes(std::vector<float>& xs, std::vector<float>& ys,
                              const std::vector<float>& widths, const std::vector<float>& heights, bool horizontal) {
    const std::size_t n = xs.size();
    if (n < 3 || ys.size() != n) return;
    std::vector<float>& pos = horizontal ? xs : ys;
    const std::vector<float>& size = horizontal ? widths : heights;
    auto sz = [&](std::size_t i) { return i < size.size() ? size[i] : 0.0f; };
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return pos[a] < pos[b]; });
    const float start = pos[order.front()], end = pos[order.back()] + sz(order.back());
    float total = 0.0f;
    for (std::size_t i : order) total += sz(i);
    const float gap = (end - start - total) / (float)(n - 1);
    float at = start;
    for (std::size_t i : order) { pos[i] = at; at += sz(i) + gap; }
}

/// Straighten a wire: the new y of the block at the wire's far end so its plug sits level with the near plug.
/// Rows are the plugs' row numbers on each block (row 0 is the first plug row under the header); `rowH` is a row's
/// height in the same units as the y values.
inline float VsStraightenY(float srcY, int srcRow, float dstY, int dstRow, float rowH) {
    (void)dstY;
    return srcY + (float)(srcRow - dstRow) * rowH;
}

/// One wire for auto-arrange: `from` runs or feeds `to`.
struct VsArrangeEdge { int from = 0, to = 0; };

/// Auto-arrange: blocks in columns by how far along the flow they are (a block's column is one more than the furthest
/// block wired into it), each column stacked top to bottom in the order the blocks were in, the whole group keeping
/// its top-left corner. A value block lands in the column just before the first block that reads it. Wires that loop
/// back are ignored. Returns the new positions in the order of `ids`.
inline std::vector<std::pair<float, float>> VsAutoArrange(const std::vector<int>& ids, const std::vector<float>& xs,
                                                          const std::vector<float>& ys, const std::vector<float>& widths,
                                                          const std::vector<float>& heights,
                                                          const std::vector<VsArrangeEdge>& edges,
                                                          float gapX = 80.0f, float gapY = 30.0f) {
    const std::size_t n = ids.size();
    std::vector<std::pair<float, float>> out(n);
    if (n == 0 || xs.size() != n || ys.size() != n) return out;
    auto idx = [&](int id) { for (std::size_t i = 0; i < n; ++i) if (ids[i] == id) return (int)i; return -1; };
    std::vector<int> col(n, 0);
    // Longest path from the starts, at most n rounds (a loop back can't push a column forever).
    for (std::size_t round = 0; round < n; ++round) {
        bool moved = false;
        for (const VsArrangeEdge& e : edges) {
            const int a = idx(e.from), b = idx(e.to);
            if (a < 0 || b < 0 || a == b) continue;
            if (col[(std::size_t)b] < col[(std::size_t)a] + 1 && col[(std::size_t)a] + 1 < (int)n) { col[(std::size_t)b] = col[(std::size_t)a] + 1; moved = true; }
        }
        if (!moved) break;
    }
    // A value block with no inputs sits just before its first reader, not at the far left.
    for (std::size_t i = 0; i < n; ++i) {
        bool hasIn = false; int firstReader = 1 << 30;
        for (const VsArrangeEdge& e : edges) {
            if (idx(e.to) == (int)i && idx(e.from) >= 0) hasIn = true;
            if (idx(e.from) == (int)i && idx(e.to) >= 0) firstReader = std::min(firstReader, col[(std::size_t)idx(e.to)]);
        }
        if (!hasIn && firstReader < (1 << 30) && firstReader - 1 > col[i]) col[i] = firstReader - 1;
    }
    float x0 = xs[0], y0 = ys[0];
    for (std::size_t i = 0; i < n; ++i) { x0 = std::min(x0, xs[i]); y0 = std::min(y0, ys[i]); }
    int maxCol = 0;
    for (int c : col) maxCol = std::max(maxCol, c);
    float x = x0;
    for (int c = 0; c <= maxCol; ++c) {
        std::vector<std::size_t> here;
        for (std::size_t i = 0; i < n; ++i) if (col[i] == c) here.push_back(i);
        if (here.empty()) continue;
        std::stable_sort(here.begin(), here.end(), [&](std::size_t a, std::size_t b) { return ys[a] < ys[b]; });
        float y = y0, w = 0.0f;
        for (std::size_t i : here) {
            out[i] = {x, y};
            y += (i < heights.size() ? heights[i] : 0.0f) + gapY;
            w = std::max(w, i < widths.size() ? widths[i] : 0.0f);
        }
        x += w + gapX;
    }
    return out;
}

enum class VsVarScope { Object, Scene, Global, Local, Story, OtherObject };

struct VsVarRef {
    std::string name;
    VsVarScope scope = VsVarScope::Object;
    int nodeId = -1;
};

inline const char* VsVarScopeName(VsVarScope s) {
    switch (s) {
        case VsVarScope::Object:      return "On this object";
        case VsVarScope::Scene:       return "In this scene";
        case VsVarScope::Global:      return "In the whole game";
        case VsVarScope::Local:       return "Inside one action";
        case VsVarScope::Story:       return "In the story";
        case VsVarScope::OtherObject: return "On another object";
    }
    return "";
}

inline bool VsVarScopeFor(const std::string& type, VsVarScope& out) {
    if (type == "GetVar" || type == "SetVar" || type == "AddVar" || type == "HasVar" || type == "OnVarChanged") {
        out = VsVarScope::Object; return true;
    }
    if (type == "GetSceneVar" || type == "SetSceneVar" || type == "AddSceneVar" || type == "OnSceneVarChanged") {
        out = VsVarScope::Scene; return true;
    }
    if (type == "GetGlobalVar" || type == "SetGlobalVar" || type == "AddGlobalVar" || type == "OnGlobalVarChanged") {
        out = VsVarScope::Global; return true;
    }
    if (type == "GetLocal" || type == "SetLocal") { out = VsVarScope::Local; return true; }
    if (type == "GetStoryVar" || type == "SetStoryVar") { out = VsVarScope::Story; return true; }
    if (type == "GetObjectVar" || type == "SetObjectVar") { out = VsVarScope::OtherObject; return true; }
    return false;
}

struct VsVarNode {
    std::string type;
    std::string arg;
    int id = 0;
};

inline std::string VsTrim(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

/// One row per name within a scope. The node id is the first node that uses it,
/// which is what the side panel jumps to.
inline std::vector<VsVarRef> VsCollectVariables(const std::vector<VsVarNode>& nodes) {
    std::vector<VsVarRef> out;
    for (const auto& n : nodes) {
        VsVarScope scope;
        if (!VsVarScopeFor(n.type, scope)) continue;
        std::string name = VsTrim(n.arg);
        if (name.empty()) name = "(unnamed)";
        bool seen = false;
        for (const auto& e : out)
            if (e.name == name && e.scope == scope) { seen = true; break; }
        if (!seen) out.push_back({name, scope, n.id});
    }
    std::stable_sort(out.begin(), out.end(), [](const VsVarRef& a, const VsVarRef& b) {
        if (a.scope != b.scope) return static_cast<int>(a.scope) < static_cast<int>(b.scope);
        return a.name < b.name;
    });
    return out;
}

struct VsParamRef {
    std::string function;
    std::string name;
    std::string type;
    int nodeId = -1;
};

/// Parameters declared on Function nodes (`Damage(amount: Number)`).
inline std::vector<VsParamRef> VsCollectParameters(const std::vector<VsVarNode>& nodes) {
    std::vector<VsParamRef> out;
    for (const auto& n : nodes) {
        if (n.type != "FunctionStart") continue;
        okay::vs::FunctionSig sig;
        if (!okay::vs::ParseFunctionSignature(n.arg, sig)) continue;
        if (sig.params.empty()) {
            out.push_back({sig.name.empty() ? std::string("(unnamed)") : sig.name, std::string(), std::string(), n.id});
            continue;
        }
        for (const auto& p : sig.params)
            out.push_back({sig.name, p.name, okay::vs::PinTypeName(p.type), n.id});
    }
    return out;
}

} // namespace okay::editor::vsed
