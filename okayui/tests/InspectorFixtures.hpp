#pragma once
// A small stand-in for the editor around the Inspector (stage 1, docs/own_ui_toolkit.md section 14): objects with the values the ported
// parts show, a selection, an undo stack, the project's tags and layer names, and the rules the editor applies to each intent
// (editor/main.cpp DrawInspector and CompHeader): edits go to every selected object, Lock and the resets record an undo step, a drag
// of the Transform records one step when it begins, uniform scale, the component menu. Used by the OkayUI test
// (okayui/tests/test_okayui_inspector.cpp) and the comparison with Dear ImGui (tests/test_okayui_inspector_imgui.cpp). Std and the model
// only.
#include "EditorPanelModels.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace inspfix {

namespace P = okaypanels;

struct Comp {
    std::string label;
    bool enabled = true, open = true, removable = true;
    std::uint32_t color = P::HierCol(107, 168, 250);
    int glyph = P::InspGlyphSquare;
    int diag = 0;
};

struct Obj {
    std::string name;
    bool active = true, isStatic = false, locked = false;
    std::string tag;
    int layer = 0;
    int icon = P::HierIconMesh;
    bool camera = false;
    float pos[3] = {0, 0, 0}, rot[3] = {0, 0, 0}, scale[3] = {1, 1, 1};
    P::MeshRendererValues mesh;
    bool hasMesh = true;
    std::vector<Comp> comps;   // after the Transform (the Mesh Renderer first when hasMesh)
};

struct Scene {
    std::vector<Obj> objs;
    std::vector<int> sel = {0};             // selected objects; the first is the one shown
    std::vector<std::vector<Obj>> undo;     // whole-scene steps
    std::vector<std::string> builtin = {"Player", "Enemy", "MainCamera", "Respawn", "Finish"};
    std::vector<std::string> custom = {"Pickup", "Door"};
    std::vector<std::string> layers;
    std::vector<std::string> log;
    bool uniform = false, transformOpen = true, copyValid = false;
    float copyPos[3] = {0, 0, 0}, copyRot[3] = {0, 0, 0}, copyScale[3] = {1, 1, 1};
    char nameBuf[128] = {};
    P::InspectorHeaderState header;
    int addComponentOpened = 0;
    std::string menuChosen;

    Scene() {
        layers.resize(32);
        const char* named[] = {"Default", "TransparentFX", "Ignore Raycast", "", "Water", "UI"};
        for (int i = 0; i < 32; ++i) layers[(std::size_t)i] = i < 6 && named[i][0] ? named[i] : (i < 6 ? "" : "");
        for (int i = 0; i < 32; ++i) if (layers[(std::size_t)i].empty()) { char b[16]; std::snprintf(b, sizeof b, "Layer %d", i); layers[(std::size_t)i] = b; }
    }
    Obj& Shown() { return objs[(std::size_t)sel.front()]; }
    void PushUndo() { undo.push_back(objs); }
    bool Undo() { if (undo.empty()) return false; objs = undo.back(); undo.pop_back(); return true; }
    template <class F> void Each(F f) { for (int i : sel) f(objs[(std::size_t)i]); }

    // ---- models
    P::InspectorHeaderModel Header() {
        Obj& o = Shown();
        std::snprintf(nameBuf, sizeof nameBuf, "%s", o.name.c_str());
        P::InspectorHeaderModel m;
        m.active = o.active; m.name = nameBuf; m.nameSize = sizeof nameBuf;
        m.isStatic = o.isStatic; m.locked = o.locked; m.icon = o.icon; m.tag = o.tag;
        m.layer = o.layer; m.layerName = layers[(std::size_t)o.layer];
        m.tags = [this](std::vector<std::string>& b, std::vector<std::string>& c) { b = builtin; c = custom; };
        m.layerNameOf = [this](int i) { return i >= 0 && i < 32 ? layers[(std::size_t)i] : std::string(); };
        m.layerRenameInfo = [this](int i, std::string& text, std::string& hint) {
            text = i >= 0 && i < 32 ? layers[(std::size_t)i] : std::string();
            hint = "Layer " + std::to_string(i);
        };
        return m;
    }
    P::TransformModel Transform() {
        Obj& o = Shown();
        P::TransformModel m;
        std::copy(o.pos, o.pos + 3, m.pos); std::copy(o.rot, o.rot + 3, m.rot); std::copy(o.scale, o.scale + 3, m.scale);
        m.open = transformOpen; m.isCamera = o.camera; m.uniform = uniform; m.selectedCount = (int)sel.size(); m.pasteValid = copyValid;
        return m;
    }
    std::vector<int> mixedFields() {
        // which shading values differ on the selection (the editor's DrawMixedNote names them from the property registry)
        std::vector<int> out;
        if (sel.size() < 2) return out;
        const Obj& a = objs[(std::size_t)sel[0]];
        for (int i : sel) {
            const Obj& b = objs[(std::size_t)i];
            if (std::memcmp(a.mesh.color, b.mesh.color, sizeof a.mesh.color) != 0 && std::find(out.begin(), out.end(), 0) == out.end()) out.push_back(0);
            if (a.mesh.castShadows != b.mesh.castShadows && std::find(out.begin(), out.end(), 1) == out.end()) out.push_back(1);
        }
        return out;
    }
    P::CompHeaderModel CompHeader(std::size_t i) {
        const Comp& c = Shown().comps[i];
        P::CompHeaderModel m;
        m.label = c.label; m.enabled = c.enabled; m.open = c.open; m.color = c.color; m.glyph = c.glyph; m.diag = c.diag;
        m.sectionGap = 10.0f;
        if (c.diag) m.diagTip = "1 problem";
        if (i == 0 && Shown().hasMesh) {
            const std::vector<int> mixed = mixedFields();
            if (!mixed.empty()) {
                m.mixedNote = "Different on the selected objects: ";
                for (std::size_t k = 0; k < mixed.size(); ++k) m.mixedNote += std::string(k ? ", " : "") + (mixed[k] == 0 ? "Color" : "Cast Shadows");
            }
        }
        const bool removable = c.removable;
        m.buildMenu = [removable](std::vector<P::PanelMenuEntry>& out) {
            P::CompMenuHead(out);
            P::PanelMenuEntry sep; sep.kind = P::PanelMenuSeparator; out.push_back(sep);
            P::PanelMenuEntry e;
            e.id = P::kCompMenuCopy; e.label = "Copy Values"; out.push_back(e);
            e.id = P::kCompMenuPaste; e.label = "Paste Values"; e.enabled = false; e.reason = "Copy the values of another component of this kind first."; out.push_back(e);
            e = P::PanelMenuEntry(); e.id = P::kCompMenuReset; e.label = "Reset"; out.push_back(e);
            P::CompMenuTail(out, removable);
        };
        return m;
    }
    P::MeshRendererModel Mesh(P::MeshRendererValues& scratch) {
        scratch = Shown().mesh;
        P::MeshRendererModel m;
        m.v = &scratch; m.verts = 117; m.tris = 192; m.uvs = true;
        return m;
    }

    // ---- intents, as the editor carries them out
    void Apply(const P::InspectorHeaderIntent& in) {
        Obj& o = Shown();
        if (in.toggleActive) { const bool v = !o.active; Each([&](Obj& g) { g.active = v; }); }
        if (in.nameChanged) o.name = nameBuf;
        if (in.toggleStatic) o.isStatic = !o.isStatic;
        if (in.toggleLock) { PushUndo(); o.locked = !o.locked; }
        if (in.setTag) o.tag = in.tag;
        if (in.addTag) {
            if (in.newTag.size() > 32) header.tagError = "too long";
            else {
                if (std::find(custom.begin(), custom.end(), in.newTag) == custom.end()) custom.push_back(in.newTag);
                o.tag = in.newTag; header.newTag[0] = '\0'; header.tagError.clear(); header.closeTagList = true;
            }
        }
        if (in.setLayer >= 0) o.layer = in.setLayer;
        if (in.savePrefab) log.push_back("Saved prefab " + o.name + ".okayprefab");
        if (in.renameTag) {
            if (std::find(custom.begin(), custom.end(), in.tagTo) != custom.end()) header.renameError = "that tag is already in the list";
            else {
                PushUndo();
                for (std::string& t : custom) if (t == in.tagFrom) t = in.tagTo;
                for (Obj& g : objs) if (g.tag == in.tagFrom) g.tag = in.tagTo;
                header.closeRename = true;
            }
        }
        if (in.removeTag) custom.erase(std::remove(custom.begin(), custom.end(), in.removeTagName), custom.end());
        if (in.renameLayer && in.layerIndex >= 0 && in.layerIndex < 32) { layers[(std::size_t)in.layerIndex] = in.layerTo; header.closeRename = true; }
        if (in.resetLayer && in.layerIndex >= 0 && in.layerIndex < 32) { layers[(std::size_t)in.layerIndex] = "Layer " + std::to_string(in.layerIndex); header.closeRename = true; }
    }
    void Apply(const P::TransformIntent& in, const P::TransformModel& m) {
        if (in.activated) PushUndo();
        if (in.posChanged) Each([&](Obj& g) { std::copy(m.pos, m.pos + 3, g.pos); });
        if (in.rotChanged) Each([&](Obj& g) { std::copy(m.rot, m.rot + 3, g.rot); });
        if (in.scaleChanged) Each([&](Obj& g) { if (!g.camera) std::copy(m.scale, m.scale + 3, g.scale); });
        if (in.resetPos) { PushUndo(); Obj& o = Shown(); o.pos[0] = o.pos[1] = o.pos[2] = 0; }
        if (in.resetRot) { PushUndo(); Obj& o = Shown(); o.rot[0] = o.rot[1] = o.rot[2] = 0; }
        if (in.resetScale) { PushUndo(); Obj& o = Shown(); o.scale[0] = o.scale[1] = o.scale[2] = 1; }
        if (in.toggleUniform) uniform = !uniform;
        if (in.toggleOpen) transformOpen = !transformOpen;
        if (in.copy) { Obj& o = Shown(); std::copy(o.pos, o.pos + 3, copyPos); std::copy(o.rot, o.rot + 3, copyRot); std::copy(o.scale, o.scale + 3, copyScale); copyValid = true; }
        if (in.paste && copyValid) { PushUndo(); Obj& o = Shown(); std::copy(copyPos, copyPos + 3, o.pos); std::copy(copyRot, copyRot + 3, o.rot); std::copy(copyScale, copyScale + 3, o.scale); }
    }
    void Apply(std::size_t i, const P::CompHeaderIntent& in) {
        std::vector<Comp>& cs = Shown().comps;
        if (i >= cs.size()) return;
        if (in.toggleEnabled) cs[i].enabled = !cs[i].enabled;
        if (in.toggleOpen) cs[i].open = !cs[i].open;
        if (!in.menuId.empty()) {
            menuChosen = in.menuId;
            if (in.menuId == P::kCompMenuRemove) { PushUndo(); cs.erase(cs.begin() + (long)i); if (i == 0) Shown().hasMesh = false; }
            else if (in.menuId == P::kCompMenuDown && i + 1 < cs.size()) { PushUndo(); std::swap(cs[i], cs[i + 1]); }
            else if (in.menuId == P::kCompMenuCollapse) for (Comp& c : cs) c.open = false;
            else if (in.menuId == P::kCompMenuExpand) for (Comp& c : cs) c.open = true;
        }
    }
    void Apply(const P::MeshRendererIntent& in, const P::MeshRendererValues& v) {
        if (!in.changed) return;
        // what a drawer changes on the shown object goes to the other selected ones too (the editor's PropagateComponentEdit)
        const P::MeshRendererValues before = Shown().mesh;
        Shown().mesh = v;
        for (std::size_t k = 1; k < sel.size(); ++k) {
            P::MeshRendererValues& o = objs[(std::size_t)sel[k]].mesh;
            if (std::memcmp(before.color, v.color, sizeof v.color) != 0) std::copy(v.color, v.color + 4, o.color);
            if (before.castShadows != v.castShadows) o.castShadows = v.castShadows;
            if (before.receiveShadows != v.receiveShadows) o.receiveShadows = v.receiveShadows;
            if (before.shader != v.shader) o.shader = v.shader;
            if (before.rimStrength != v.rimStrength) o.rimStrength = v.rimStrength;
            if (before.primitive != v.primitive) o.primitive = v.primitive;
        }
    }
};

/// The sample object the pictures show: a crate with a Mesh Renderer (the shading of the screenshot in the brief: grey colour, the
/// shadows on, a ground shadow), and three more components below it (closed), one of them off and one with a problem.
inline Scene Sample() {
    Scene s;
    Obj o;
    o.name = "7 Hook"; o.icon = P::HierIconMesh;
    o.pos[0] = -7.0f; o.pos[1] = 7.5f; o.pos[2] = 15.0f;
    o.scale[0] = o.scale[1] = o.scale[2] = 0.3f;
    o.mesh.color[0] = 160 / 255.0f; o.mesh.color[1] = 170 / 255.0f; o.mesh.color[2] = 185 / 255.0f; o.mesh.color[3] = 1.0f;
    o.mesh.castShadows = o.mesh.receiveShadows = o.mesh.groundShadow = true;
    o.mesh.groundShadowStrength = 0.5f; o.mesh.primitive = 0;
    Comp mr; mr.label = "Mesh Renderer"; mr.glyph = P::InspGlyphSquare;
    Comp rb; rb.label = "Rigidbody 3D"; rb.color = P::HierCol(107, 204, 128); rb.glyph = P::InspGlyphCircle; rb.open = false;
    Comp bc; bc.label = "Box Collider 3D"; bc.color = P::HierCol(107, 204, 128); bc.glyph = P::InspGlyphCircle; bc.open = false; bc.enabled = false;
    Comp sc; sc.label = "Script (hook_swing.okay)"; sc.color = P::HierCol(230, 204, 89); sc.glyph = P::InspGlyphDiamond; sc.open = false; sc.diag = 2;
    o.comps = {mr, rb, bc, sc};
    s.objs.push_back(o);
    Obj cam;
    cam.name = "Main Camera"; cam.icon = P::HierIconCamera; cam.camera = true; cam.hasMesh = false; cam.pos[1] = 2.0f; cam.pos[2] = -10.0f;
    Comp cc; cc.label = "Camera"; cc.glyph = P::InspGlyphSquare; cc.open = false; cc.removable = false;
    cam.comps = {cc};
    s.objs.push_back(cam);
    Obj o2 = o;
    o2.name = "7 Hook (2)"; o2.pos[0] = 3.0f; o2.mesh.castShadows = false; o2.mesh.color[0] = 0.5f;
    s.objs.push_back(o2);
    return s;
}

/// The Inspector's spacing at the Comfortable density and 100% (editor/EditorLayoutMath.hpp ComputeInspectorMetrics): items 10 across and
/// 12 down, frames 9 and 8, inner spacing 8 and 6; the window keeps the editor's 16 and 12 padding.
struct InspectorMetrics {
    float itemX = 10.0f, itemY = 12.0f, frameX = 9.0f, frameY = 8.0f, innerX = 8.0f, innerY = 6.0f;
};
/// The value column of the component fields: the panel less a label column 46% of it, between 128 and 190 pixels (DrawInspector's
/// PushItemWidth(-inspLabelW)).
inline float InspectorItemWidth(float avail) { return avail - std::clamp(avail * 0.46f, 128.0f, 190.0f); }

} // namespace inspfix
