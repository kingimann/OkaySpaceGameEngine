#pragma once
// ---------------------------------------------------------------------------
// Visual-script nodes for in-game build mode: enter / leave it, set the palette,
// save / load a creation. The events (On Object Placed, On Object Removed, On
// Logic Signal) are plain EventNodes that BuildMode and LogicTarget fire.
// ---------------------------------------------------------------------------
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/Components/BuildMode.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include <string>

namespace okay::vs {

inline BuildMode* VsBuildMode(VsContext& ctx) {
    Scene* s = ctx.gameObject ? ctx.gameObject->scene() : nullptr;
    return s ? s->FindObjectOfType<BuildMode>() : nullptr;
}

// Enter / Exit Build Mode.
struct BuildModeSetNode : VsNode {
    bool on;
    explicit BuildModeSetNode(bool o) : on(o) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (BuildMode* m = VsBuildMode(ctx)) m->SetActive(on);
        else g.Error(this, ctx, "there's no Build Mode component in the scene (add one to the player)");
        return 0;
    }
};

// In Build Mode?
struct BuildModeActiveNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        BuildMode* m = VsBuildMode(ctx);
        return VsValue(m && m->Active());
    }
};

// Set Build Palette: replaces the palette with the node's list,
// "Name=prefab|icon, Name=prefab, ..." (the icon is optional).
struct BuildSetPaletteNode : VsNode {
    std::string spec;
    explicit BuildSetPaletteNode(std::string s) : spec(std::move(s)) {}
    static std::string Trim(const std::string& s) {
        std::size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    }
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        BuildMode* m = VsBuildMode(ctx);
        if (!m) { g.Error(this, ctx, "there's no Build Mode component in the scene"); return 0; }
        m->ClearPalette();
        std::size_t pos = 0;
        while (pos <= spec.size()) {
            std::size_t comma = spec.find(',', pos);
            std::string item = Trim(spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
            pos = comma == std::string::npos ? spec.size() + 1 : comma + 1;
            if (item.empty()) continue;
            std::string name = item, prefab = item, icon;
            const std::size_t eq = item.find('=');
            if (eq != std::string::npos) { name = Trim(item.substr(0, eq)); prefab = Trim(item.substr(eq + 1)); }
            const std::size_t bar = prefab.find('|');
            if (bar != std::string::npos) { icon = Trim(prefab.substr(bar + 1)); prefab = Trim(prefab.substr(0, bar)); }
            m->AddToPalette(name, prefab, icon);
        }
        return 0;
    }
};

// Save / Load Creation: the node's text is the creation name; the Bool output
// says whether it worked.
struct BuildCreationNode : VsNode {
    bool load;
    std::string name;
    bool ok = false;
    BuildCreationNode(bool l, std::string n) : load(l), name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return VsValue(ok); }
    int Exec(NodeGraph&, VsContext& ctx) override {
        ok = false;
        Scene* s = ctx.gameObject ? ctx.gameObject->scene() : nullptr;
        if (!s) return 0;
        if (BuildMode* m = VsBuildMode(ctx)) ok = load ? m->LoadCreation(name) : m->SaveCreation(name);
        else ok = load ? BuildCreation::Load(*s, BuildMode::CreationPath(name))
                       : BuildCreation::Save(*s, BuildMode::CreationPath(name));
        return 0;
    }
};

} // namespace okay::vs
