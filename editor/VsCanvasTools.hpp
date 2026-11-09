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

enum class VsAlign { Left, Right, Top, Bottom };

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
        }
    }
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
        case VsVarScope::Scene:       return "Scene";
        case VsVarScope::Global:      return "Global";
        case VsVarScope::Local:       return "Local";
        case VsVarScope::Story:       return "Story";
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
