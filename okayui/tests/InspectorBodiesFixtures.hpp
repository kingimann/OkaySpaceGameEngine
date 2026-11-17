#pragma once
// Component bodies as field rows for the Inspector's stage 2 tests (docs/own_ui_toolkit.md section 15): stand-ins for the values of a
// Spot light, a camera, a Rigidbody 3D, a Box Collider 3D, a script's variables, the rest of a Mesh Renderer and a component drawn from
// the property registry, and the rows the editor's builders make of them (editor/main.cpp DrawLightWithOkayUI and the others,
// editor/InspectorPanel.cpp DrawFallbackOkayUI): the same kinds, labels, ranges, formats and layout. Used by the OkayUI test
// (okayui/tests/test_okayui_inspector.cpp) and the comparison with Dear ImGui (tests/test_okayui_inspector_imgui.cpp). Std and the
// model only; the hooks answer from small tables and count what they were asked.
#include "EditorPanelModels.hpp"

#include <string>
#include <vector>

namespace inspbody {

namespace P = okaypanels;

enum Body : int { BodyLight = 0, BodyCamera, BodyRigidbody, BodyBoxCollider, BodyScript, BodyMeshRest, BodyGeneric, BodyCount };
inline const char* const kBodyNames[BodyCount] = {"light", "camera", "rigidbody", "boxcollider", "script", "meshrest", "generic"};

struct Values {
    // Light (a Spot light, colour mode)
    int lightType = 2; bool useTemp = false; float temperature = 6500.0f; float color[4] = {1.0f, 0.95f, 0.8f, 1.0f};
    float intensity = 1.2f, ambient = 0.2f, ambientTint[3] = {0.5f, 0.55f, 0.65f}, range = 12.0f; int falloff = 1;
    float spotAngle = 45.0f, softness = 0.3f; bool castShadows = true; float shStrength = 0.8f, shBias = 1.0f;
    float flicker = 0.3f, flickerSpeed = 9.0f;
    // Camera (perspective, a solid colour background)
    int projection = 1; bool physical = false; float fov = 60.0f; int fovAxis = 0; int clearFlags = 1; float bg[4] = {0.16f, 0.12f, 0.24f, 1.0f};
    float nearClip = 0.1f, farClip = 500.0f, zoom = 1.0f, dutch = 0.0f, shift[2] = {0.0f, 0.0f};
    float shakeAmount = 0.5f, shakeAngle = 2.0f, shakeFreq = 25.0f, shakeRecovery = 3.0f, rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    int cullingMask = ~0; bool mainCam = true; float depth = 0.0f; std::string renderTarget;
    // Rigidbody 3D
    int bodyType = 0; float gravity = 1.0f, mass = 1.0f, drag = 0.0f, bounce = 0.2f, friction = 0.5f, maxFall = 0.0f;
    bool fx = false, fy = false, fz = false, freezeRot = false; float angDrag = 0.05f; bool sleep = true, ccd = false; float rolling = 0.03f;
    bool children = false;
    // Box Collider 3D
    float size[3] = {1.0f, 1.0f, 1.0f}, offset[3] = {0.0f, 0.5f, 0.0f}; bool trigger = true; int layer = 0;
    bool ownF = true, ownB = false; float colFriction = 0.6f; int fmix = 0, bmix = 0; std::string onlyTag = "Player", onlyLayers;
    bool autoFit = false, editCollider = false;
    // a script's variables
    bool vAlive = true; float vSpeed = 5.0f, vJump = 0.4f, vTarget[3] = {1.0f, 2.0f, 0.0f}; std::string vName = "Hero", vFollow = "Player";
    // the rest of a Mesh Renderer
    std::string meshFile, texture = "Assets/stone.png", normalMap, specMap, aoMap, mrMap, emisMap, matPath = "Assets/Material.okaymat", procPick;
    float tiling[2] = {1.0f, 1.0f}, texOffset[2] = {0.0f, 0.0f}, uvScroll[2] = {0.0f, 0.0f}; int texFilter = 0; bool triplanar = false;
    float reflect = 0.0f, metallic = 0.0f;
    // a component from the property registry (an Audio Source)
    std::string clip = "Assets/Audio/step.wav", follow;   // the follow field holds an object's guid
    bool playOnAwake = true, loop = false; float volume = 0.8f; int priority = 128; float pos3[3] = {0.0f, 1.0f, 0.0f};
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f}; int rolloff = 1; std::string note = "footsteps";

    // what the rows asked for
    int tipAsked = 0, choicesAsked = 0, prefabMenuAsked = 0;
    std::vector<std::string> clicked;   // the buttons pressed, by label
};

inline const char* const kLightTypes[] = {"Directional", "Point", "Spot", "Area"};
inline const char* const kFalls[] = {"Linear", "Inverse Square"};
inline const char* const kProjs[] = {"Orthographic", "Perspective"};
inline const char* const kAxes[] = {"Vertical", "Horizontal"};
inline const char* const kClears[] = {"Skybox", "Solid Color", "Depth Only", "Don't Clear"};
inline const char* const kBodyTypes[] = {"Dynamic", "Kinematic", "Static"};
inline const char* const kModes[] = {"Default", "Average", "Minimum", "Multiply", "Maximum"};
inline const char* const kFilters[] = {"Smooth", "Pixel"};
inline const char* const kRolloffs[] = {"Linear", "Logarithmic", "None"};
inline const char* const kBgIds[] = {"##bgp0", "##bgp1", "##bgp2", "##bgp3", "##bgp4"};
inline const float kBgCols[5][3] = {{0.36f, 0.58f, 0.85f}, {0.16f, 0.12f, 0.24f}, {0.02f, 0.03f, 0.07f}, {1, 1, 1}, {0, 0, 0}};

/// The hooks the views ask: tooltips, the prefab menu, and what pickers list (counted in `v`).
inline P::InspFieldHooks Hooks(Values& v) {
    P::InspFieldHooks h;
    h.tipFor = [&v](const char* label) { ++v.tipAsked; return std::string("Tip for ") + std::string(label, P::InspVisibleLen(label)); };
    h.prefabMenu = [&v](const char* label, std::vector<P::PanelMenuEntry>& out) {
        ++v.prefabMenuAsked;
        P::PanelMenuEntry e; e.kind = P::PanelMenuHeading; e.label = "Overridden from Crate.okayprefab"; out.push_back(e);
        e.label = std::string(label, P::InspVisibleLen(label)) + ": 1 -> 2"; out.push_back(e);
        P::PanelMenuEntry sep; sep.kind = P::PanelMenuSeparator; out.push_back(sep);
        P::PanelMenuEntry r; r.id = P::kInspPrefabRevert; r.label = "Revert Property"; out.push_back(r);
        r.id = P::kInspPrefabApply; r.label = "Apply Property to Prefab"; out.push_back(r);
    };
    h.choices = [&v](const P::InspRow& row, std::vector<std::string>& values, std::vector<std::string>& names, std::vector<void*>& thumbs) {
        ++v.choicesAsked;
        const std::string k = row.pickKind ? row.pickKind : "";
        if (k == "layers") {
            const char* named[] = {"Default", "TransparentFX", "Ignore Raycast", "Layer 3", "Water", "UI"};
            for (int i = 0; i < 32; ++i) names.push_back(i < 6 ? named[i] : "Layer " + std::to_string(i));
        } else if (k == "proc") {
            for (const char* n : {"brick", "wood", "stone", "grass", "metal", "tiles"}) { values.push_back(std::string("proc:") + n); names.push_back(n); thumbs.push_back(nullptr); }
        } else if (k == "object") {
            const char* objs[][2] = {{"g-1", "Player"}, {"g-2", "Main Camera"}, {"g-3", "Door  (in House)"}};
            for (auto& o : objs) { values.push_back(o[0]); names.push_back(o[1]); }
        } else {
            for (const char* f : {"Assets/stone.png", "Assets/wood.png", "Assets/ui/icon.png", "Assets/Audio/step.wav", "Assets/Audio/jump.wav"}) {
                values.push_back(f); names.push_back(std::string(f).substr(7));
            }
        }
    };
    return h;
}

/// The rows of one body (built every frame, as the editor's builders are), pointing at `v`.
inline P::InspFieldList Rows(Body b, Values& v) {
    P::InspFieldList L;
    L.hooks = Hooks(v);
    switch (b) {
    case BodyLight: {
        L.Combo("Type##light", &v.lightType, kLightTypes, 4); L.Tip("Directional = sun (parallel).\nPoint = bulb (all directions).");
        L.Check("Use Temperature##light", &v.useTemp); L.TipLookup();
        L.Color("Color##light", v.color, 4); L.TipLookup();
        L.Drag("Intensity##light", &v.intensity, 1, 0.02f, 0.0f, 8.0f); L.TipLookup();
        L.Section("Ambient (scene floor)");
        L.Slider("Ambient##light", &v.ambient, 0.0f, 1.0f); L.Tip("Unlit floor brightness (taken from the first light)");
        L.Color("Ambient Tint##light", v.ambientTint, 3); L.TipLookup();
        if (v.lightType != 0) {
            L.Section(v.lightType == 2 ? "Spot" : v.lightType == 3 ? "Area" : "Point");
            L.Drag("Range##light", &v.range, 1, 0.2f, 0.1f, 500.0f); L.TipLookup();
            L.Combo("Falloff##light", &v.falloff, kFalls, 2);
        }
        if (v.lightType == 2) {
            L.Slider("Spot Angle##light", &v.spotAngle, 5.0f, 170.0f); L.TipLookup();
            L.Slider("Softness##light", &v.softness, 0.0f, 1.0f);
        }
        L.Section("Shadows");
        L.Check("Cast Shadows##light", &v.castShadows);
        if (v.castShadows) { L.Slider("Strength##lightsh", &v.shStrength, 0.0f, 1.0f); L.Slider("Bias##lightsh", &v.shBias, 0.0f, 4.0f); }
        L.Section("Flicker");
        L.Slider("Amount##lightfl", &v.flicker, 0.0f, 1.0f);
        if (v.flicker > 0.0f) {
            L.Slider("Speed##lightfl", &v.flickerSpeed, 0.5f, 30.0f, "%.1f /s"); L.TipLookup();
            L.SmallButton("Candle"); L.SameLine(4.0f); L.SmallButton("Torch"); L.SameLine(4.0f); L.SmallButton("Faulty bulb");
            L.Note("Flicker plays in Play mode and in the built game.");
        }
        L.Note("Rotate this object to aim the light (its +Z is the direction).");
        L.SmallButton("Remove##light");
        break;
    }
    case BodyCamera: {
        L.Combo("Projection", &v.projection, kProjs, 2); L.TipLookup();
        L.Check("Physical Camera", &v.physical);
        L.Slider("Field of View", &v.fov, 10.0f, 170.0f, "%.0f deg"); L.TipLookup();
        L.SmallButton("60"); L.SameLine(4.0f); L.SmallButton("75"); L.SameLine(4.0f); L.SmallButton("90"); L.SameLine(4.0f); L.SmallButton("110");
        L.Combo("FOV Axis", &v.fovAxis, kAxes, 2);
        L.Section("Background");
        L.Combo("Clear Flags", &v.clearFlags, kClears, 4);
        L.Color("Background", v.bg, 4);
        for (int i = 0; i < 5; ++i) { if (i) L.SameLine(4.0f); L.Swatch(kBgIds[i], kBgCols[i][0], kBgCols[i][1], kBgCols[i][2], 1.0f, 22.0f, 18.0f); }
        L.Section("Clipping Planes");
        L.Drag("Near", &v.nearClip, 1, 0.01f, 0.001f, 100.0f);
        L.Drag("Far", &v.farClip, 1, 1.0f, 1.0f, 100000.0f);
        L.Section("Zoom & Framing");
        L.Slider("Zoom", &v.zoom, 0.25f, 8.0f, "%.2fx", true); L.SameLine(); L.SmallButton("1x##zoomreset");
        L.Slider("Dutch Angle", &v.dutch, -45.0f, 45.0f, "%.1f deg");
        L.Drag("Lens Shift", v.shift, 2, 0.005f, -1.0f, 1.0f, "%.3f");
        L.Section("Camera Shake");
        L.Slider("Amount", &v.shakeAmount, 0.0f, 3.0f, "%.2f units");
        L.Slider("Angle", &v.shakeAngle, 0.0f, 15.0f, "%.1f deg");
        L.Button("Test Shake"); L.Last().disabled = true;
        L.Section("Viewport Rect");
        L.Drag("X / Y / W / H", v.rect, 4, 0.01f, 0.0f, 1.0f);
        L.Section("Culling Mask");
        L.Mask("Layers", &v.cullingMask);
        L.Spacing();
        L.Check("Main", &v.mainCam); L.SameLine(); L.Drag("Depth", &v.depth, 1, 0.1f); L.Last().width = 90.0f;
        L.Text("Render Target", &v.renderTarget);
        L.Button("Add overlay camera");
        L.SmallButton("Remove##cam");
        break;
    }
    case BodyRigidbody: {
        L.Combo("Body Type##rb3", &v.bodyType, kBodyTypes, 3); L.TipLookup();
        L.Drag("Gravity Scale##rb3", &v.gravity, 1, 0.05f);
        L.Drag("Mass##rb3", &v.mass, 1, 0.05f, 0.01f, 1000.0f);
        L.Drag("Drag##rb3", &v.drag, 1, 0.01f, 0.0f, 100.0f);
        L.Drag("Bounciness##rb3", &v.bounce, 1, 0.01f, 0.0f, 1.0f);
        L.Drag("Friction##rb3", &v.friction, 1, 0.01f, 0.0f, 2.0f);
        L.Drag("Max Fall Speed##rb3", &v.maxFall, 1, 0.1f, 0.0f, 200.0f);
        L.Note("Freeze position");
        L.SameLine(); L.Check("X##fz", &v.fx); L.SameLine(); L.Check("Y##fz", &v.fy); L.SameLine(); L.Check("Z##fz", &v.fz);
        L.Check("Freeze Rotation##rb3", &v.freezeRot);
        if (!v.freezeRot) L.Drag("Angular Drag##rb3", &v.angDrag, 1, 0.01f, 0.0f, 10.0f);
        L.Check("Allow Sleep##rb3", &v.sleep);
        L.Check("Continuous Collision##rb3", &v.ccd);
        L.Drag("Rolling Resistance##rb3", &v.rolling, 1, 0.005f, 0.0f, 1.0f, "%.3f");
        L.Check("Use Child Colliders##rb3", &v.children);
        L.Label("This body follows its parent. Moving or turning the parent carries it, and gravity still pulls straight down.");
        L.Last().wrap = true;
        L.SmallButton("Remove##rb3");
        break;
    }
    case BodyBoxCollider: {
        L.Drag("Size##bc3", v.size, 3, 0.05f, 0.0f, 0.0f, "%.3f"); L.Last().axisBars = true;
        L.Drag("Offset##bc3", v.offset, 3, 0.05f, 0.0f, 0.0f, "%.3f"); L.Last().axisBars = true;
        L.Check("Trigger##bc3", &v.trigger); L.SameLine(); L.DragInt("Layer##bc3", &v.layer, 0.1f, 0, 31); L.Last().width = 80.0f;
        L.Tree("Physics Material##pmbc3"); L.Last().on = true;
        L.Check("Own Friction##pmbc3", &v.ownF);
        if (v.ownF) { L.Drag("Friction##pmbc3", &v.colFriction, 1, 0.01f, 0.0f, 10.0f); L.Combo("Friction Mix##pmbc3", &v.fmix, kModes, 5); }
        L.Check("Own Bounciness##pmbc3", &v.ownB);
        L.TreeEnd();
        if (v.trigger) {
            L.Text("Only Tag##tfbc3", &v.onlyTag, "any tag (e.g. Player)");
            L.Text("Only Layers##tfbc3", &v.onlyLayers, "all layers");
        }
        L.SmallButton("Fit to mesh##bc3"); L.SameLine(); L.Check("Auto Fit##bc3", &v.autoFit); L.TipLookup();
        L.Button("Edit Collider##bc3", v.editCollider);
        if (v.editCollider) L.Note("Drag the colored dots on each face in the Scene view.");
        L.SmallButton("Remove##bc3");
        break;
    }
    case BodyScript: {
        L.SmallButton("Open in Script Editor");
        L.Section("Variables");
        const float nameCol = 200.0f;
        L.Label("alive", 0, true); L.SameLine(-1.0f, nameCol); L.Check("##v", &v.vAlive);
        L.Label("speed", P::kInspOverriddenVar, true); L.SameLine(-1.0f, nameCol); L.Drag("##v", &v.vSpeed, 1, 0.05f); L.Last().width = -28.0f;
        L.SameLine(); L.SmallButton("o");
        L.Label("jump", 0, true); L.SameLine(-1.0f, nameCol); L.Slider("##v", &v.vJump, 0.0f, 1.0f); L.Last().width = -28.0f;
        L.Label("target", 0, true); L.SameLine(-1.0f, nameCol); L.Drag("##v", v.vTarget, 3, 0.05f); L.Last().width = -28.0f;
        L.Label("title", 0, true); L.SameLine(-1.0f, nameCol); L.Text("##v", &v.vName); L.Last().width = -28.0f;
        L.Label("follow", 0, true); L.SameLine(-1.0f, nameCol); L.Button("Player##objslot", false, -28.0f); L.Last().dropKind = "objectname";
        break;
    }
    case BodyMeshRest: {
        L.Text("Mesh File##mesh", &v.meshFile); L.SameLine(); L.SmallButton("Load##obj");
        L.Text("##meshtex", &v.texture); L.Last().pick = P::InspPickList; L.Last().dropKind = ""; L.Last().width = 210.0f;
        L.SameLine(4.0f); L.Button("...##meshtexbrowse"); L.SameLine(4.0f); L.Label("Texture");
        L.SameLineOrWrap(); L.SmallButton("Procedural##tex"); L.Last().pick = P::InspPickGrid; L.Last().pickKind = "proc"; L.Last().s = &v.procPick;
        if (!v.texture.empty()) {
            L.SameLineOrWrap(); L.SmallButton("Clear##tex");
            L.Drag("Tiling##mesh", v.tiling, 2, 0.05f, 0.01f, 64.0f);
            L.Drag("Offset##mesh", v.texOffset, 2, 0.01f);
            L.Combo("Filter##mesh", &v.texFilter, kFilters, 2);
            L.Drag("UV Scroll##mesh", v.uvScroll, 2, 0.01f);
            L.Check("Triplanar##mesh", &v.triplanar);
        }
        L.Text("Normal Map##mesh", &v.normalMap); L.Last().pick = P::InspPickList; L.Last().dropKind = "";
        L.Slider("Reflectivity##mesh", &v.reflect, 0.0f, 1.0f);
        L.Slider("Metallic##mesh", &v.metallic, 0.0f, 1.0f);
        L.Text("Specular Map##mesh", &v.specMap); L.Last().pick = P::InspPickList;
        L.Text("Occlusion Map##mesh", &v.aoMap); L.Last().pick = P::InspPickList;
        L.Text("Metal / Rough##mesh", &v.mrMap); L.Last().pick = P::InspPickList;
        L.Text("Emissive Map##mesh", &v.emisMap); L.Last().pick = P::InspPickList;
        L.Note("117 verts, 192 triangles");
        L.SmallButton("Subdivide##mesh"); L.SameLine(); L.SmallButton("Subdiv+Smooth##mesh"); L.SameLine(); L.SmallButton("Smooth##mesh");
        L.SameLine(); L.SmallButton("Weld##mesh");
        L.SmallButton("Recenter##mesh"); L.SameLine(); L.SmallButton("Ground##mesh"); L.SameLine(); L.SmallButton("Fit 1u##mesh");
        L.SameLine(); L.SmallButton("Remove##mesh");
        L.Section("Material preset");
        L.Text("##matpath", &v.matPath); L.Last().pick = P::InspPickList; L.Last().dropKind = "";
        L.SameLine(); L.SmallButton("Save##mat"); L.SameLine(); L.SmallButton("Load##mat");
        L.SmallButton("Add Physics (Rigidbody + Box)##mesh");
        break;
    }
    case BodyGeneric: {
        L.Group("Playback");
        L.Asset("Clip##clip", &v.clip, "audio", "audio"); L.TipLookup();
        L.Check("Play On Awake##playOnAwake", &v.playOnAwake); L.TipLookup();
        L.Check("Loop##loop", &v.loop); L.TipLookup();
        L.Drag("Volume##volume", &v.volume, 1, 1.0f / 300.0f, 0.0f, 1.0f); L.TipLookup();
        L.DragInt("Priority##priority", &v.priority, 0.2f, 0, 256); L.TipLookup();
        L.Group("Space");
        L.Drag("Position##pos3", v.pos3, 3, 0.01f); L.Last().axisTags = true; L.Last().mixedAxes = 2u; L.TipLookup();
        L.Color("Tint##tint", v.tint, 4); L.TipLookup();
        L.Combo("Rolloff##rolloff", &v.rolloff, kRolloffs, 3); L.TipLookup();
        L.Asset("Follow##follow", &v.follow, "object", "object");
        L.Text("Note##note", &v.note); L.TipLookup();
        break;
    }
    default: break;
    }
    return L;
}

/// After a frame: the buttons pressed (by their visible label) go into v.clicked; a Mesh Renderer pick from the procedural grid becomes
/// the texture, as the editor's builder does.
inline void Apply(Body b, Values& v, P::InspFieldList& L) {
    for (P::InspRow& r : L.rows)
        if (r.clicked) v.clicked.push_back(std::string(r.label, P::InspVisibleLen(r.label)));
    if (b == BodyMeshRest)
        for (P::InspRow& r : L.rows)
            if (r.changed && r.kind == P::InspRowSmallButton && r.pick == P::InspPickGrid) v.texture = v.procPick;
}

} // namespace inspbody
