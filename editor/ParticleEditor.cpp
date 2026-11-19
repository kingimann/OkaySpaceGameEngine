// The particle editor. Written for this engine from its own Particle System (docs/particles_upgrade.md); the preview is a plain
// orbiting camera over the shared draw list, nothing taken from another editor.
#include "ParticleEditor.hpp"
#include "ParticlePresetList.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Math/Mat4.hpp"
#include <SDL.h>   // the editor draws Dear ImGui through the SDL renderer backend: a draw callback switches the blend of its textures
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace okay;

namespace oked {

namespace {

ImU32 Pack(const Color& c, float aMul = 1.0f) {
    auto q = [](float v) { return (int)(Mathf::Clamp01(v) * 255.0f + 0.5f); };
    return IM_COL32(q(c.r), q(c.g), q(c.b), q(c.a * aMul));
}

void Tip(const char* text) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text); }

// ---- live preview state (one preview at a time: the system whose Inspector is showing) ----
struct PreviewState {
    const ParticleSystem* owner = nullptr;
    bool running = true;
    float speed = 1.0f;
    float yaw = 25.0f;
    float stepRequest = 0.0f;
};
PreviewState& Preview() { static PreviewState p; return p; }

} // namespace

namespace {
// Glow (additive) needs the renderer's add blend. Dear ImGui's draw list has no blend state, but it runs a callback in
// the middle of the command stream: ours sets SDL_BLENDMODE_ADD (or back to normal) on the textures the shapes use.
struct BlendCmd { int add; int n; ImTextureID tex[8]; };
void BlendCallback(const ImDrawList*, const ImDrawCmd* cmd) {
    const BlendCmd* c = (const BlendCmd*)cmd->UserCallbackData;
    if (!c) return;
    for (int i = 0; i < c->n && i < 8; ++i)
        if (c->tex[i]) SDL_SetTextureBlendMode((SDL_Texture*)(intptr_t)c->tex[i], c->add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
}
} // namespace

void DrawParticleItemsImGui(ImDrawList* dl, const std::vector<ParticleDrawItem>& items,
                            const std::function<ImTextureID(const std::string&)>& tex) {
    // Which textures the Glow shapes draw with (the font's white pixel for plain shapes, and each picture).
    BlendCmd cmd{0, 0, {}};
    bool anyAdd = false;
    const ImTextureID white = dl->_CmdHeader.TexRef.GetTexID();
    auto addTex = [&](ImTextureID t) {
        if (!t) return;
        for (int i = 0; i < cmd.n; ++i) if (cmd.tex[i] == t) return;
        if (cmd.n < 8) cmd.tex[cmd.n++] = t;
    };
    for (const ParticleDrawItem& it : items) {
        if (it.blend != ParticleSystem::BlendMode::Additive) continue;
        anyAdd = true;
        ImTextureID t = (it.kind == ParticleDrawItem::Quad && it.texture && tex) ? tex(*it.texture) : (ImTextureID)0;
        addTex(t ? t : white);
    }
    anyAdd = anyAdd && cmd.n > 0;
    bool addNow = false;
    auto setAdd = [&](bool add) {
        if (!anyAdd || add == addNow) return;
        BlendCmd c = cmd; c.add = add ? 1 : 0;
        dl->AddCallback(BlendCallback, &c, sizeof(c));
        addNow = add;
    };
    for (const ParticleDrawItem& it : items) {
        setAdd(it.blend == ParticleSystem::BlendMode::Additive);
        const ImU32 col = Pack(it.color);
        if (it.kind == ParticleDrawItem::Poly) {
            ImVec2 pts[4];
            for (int k = 0; k < it.npts && k < 4; ++k) pts[k] = ImVec2(it.px[k], it.py[k]);
            if (it.npts >= 3) dl->AddConvexPolyFilled(pts, it.npts, col);
            continue;
        }
        const ImVec2 p1(it.sx - it.axx - it.ayx, it.sy - it.axy - it.ayy), p2(it.sx + it.axx - it.ayx, it.sy + it.axy - it.ayy);
        const ImVec2 p3(it.sx + it.axx + it.ayx, it.sy + it.axy + it.ayy), p4(it.sx - it.axx + it.ayx, it.sy - it.axy + it.ayy);
        ImTextureID t = (it.texture && tex) ? tex(*it.texture) : (ImTextureID)0;
        if (t) dl->AddImageQuad(t, p1, p2, p3, p4, ImVec2(it.u0, it.v0), ImVec2(it.u1, it.v0), ImVec2(it.u1, it.v1), ImVec2(it.u0, it.v1), col);
        else if (it.streak) dl->AddQuadFilled(p1, p2, p3, p4, col);
        else {
            const ImVec2 c(it.sx, it.sy);
            dl->AddCircleFilled(c, it.rad * 1.6f, Pack(it.color, 0.22f), 14);   // soft outer glow
            dl->AddCircleFilled(c, it.rad * 1.15f, Pack(it.color, 0.4f), 14);
            dl->AddCircleFilled(c, it.rad * 0.75f, Pack(it.color, 0.85f), 12);  // bright core
        }
    }
    setAdd(false);
}

GameObject* CreateParticlePresetObject(okay::editor::EditorState& ed, int index) {
    if (index < 0 || index >= (int)ParticleSystem::Preset::Count) return nullptr;
    const auto pr = (ParticleSystem::Preset)index;
    GameObject* go = ed.CreateEmpty(ParticleSystem::PresetName(pr));
    auto* ps = go->AddComponent<ParticleSystem>();
    ps->ApplyPreset(pr);
    ps->playing = true;
    ps->Awake();
    ed.Select(go); ed.view3D = true; ed.dirty = true;
    return go;
}

void DrawParticlePresetMenu(okay::editor::EditorState& ed, bool& created) {
    if (!ImGui::BeginMenu("Particle Effects")) return;
    for (int i = 0; i < (int)ParticleSystem::Preset::Count; ++i) {
        const auto pr = (ParticleSystem::Preset)i;
        if (ImGui::MenuItem(ParticleSystem::PresetName(pr))) { CreateParticlePresetObject(ed, i); created = true; }
        Tip(ParticleSystem::PresetHint(pr));
    }
    ImGui::EndMenu();
}

bool DrawParticlePresetAddComponent(okay::editor::EditorState& ed, GameObject* go) {
    if (!go) return false;
    bool changed = false;
    if (ImGui::BeginMenu("Particle effect presets")) {
        for (int i = 0; i < (int)ParticleSystem::Preset::Count; ++i) {
            const auto pr = (ParticleSystem::Preset)i;
            if (ImGui::MenuItem(ParticleSystem::PresetName(pr))) {
                ed.PushUndo();
                auto* ps = go->GetComponent<ParticleSystem>();
                if (!ps) { ps = go->AddComponent<ParticleSystem>(); ps->Awake(); }
                ps->ApplyPreset(pr); ps->playing = true;
                ed.view3D = true; ed.dirty = true; changed = true;
            }
            Tip(ParticleSystem::PresetHint(pr));
        }
        ImGui::EndMenu();
    }
    return changed;
}

// ---- the Inspector rows ---------------------------------------------------------------------------------------------

static void DrawPreview(okay::editor::EditorState& ed, ParticleSystem& ps) {
    PreviewState& pv = Preview();
    if (pv.owner != &ps) { pv = PreviewState{}; pv.owner = &ps; }
    ImGuiIO& io = ImGui::GetIO();
    const float dt = std::min(io.DeltaTime, 0.1f);
    if (!ed.isPlaying()) {   // in Play the game updates it; here the preview does (the Scene view shows the same particles)
        if (pv.running) ps.Update(dt * pv.speed);
        else if (pv.stepRequest > 0.0f) ps.Update(pv.stepRequest);
    }
    pv.stepRequest = 0.0f;
    pv.yaw += dt * 18.0f;

    if (ImGui::SmallButton(pv.running ? "Pause##pspv" : "Play##pspv")) pv.running = !pv.running;
    Tip("Pause or play the preview. Press Play at the top to try the whole scene.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Restart##pspv")) { ps.Restart(); ps.playing = true; ed.dirty = true; }
    Tip("Clear the particles and start again from the beginning.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Step##pspv")) { pv.running = false; pv.stepRequest = 1.0f / 30.0f; }
    Tip("Move the effect forward by one small step (a thirtieth of a second). Handy to see what it does frame by frame.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Burst##pspv")) ps.Emit(ps.burstCount > 0 ? ps.burstCount : 20);
    Tip("Fire a quick burst of particles right now.");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Speed##pspv", &pv.speed, 0.1f, 2.0f, "%.1fx");
    Tip("How fast the preview plays. 1x is real time.");

    const float w = std::max(120.0f, ImGui::GetContentRegionAvail().x), h = 170.0f;
    const ImVec2 p0 = ImGui::GetCursorScreenPos(), p1(p0.x + w, p0.y + h);
    ImGui::InvisibleButton("##pspvview", ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p0, p1, true);
    dl->AddRectFilledMultiColor(p0, p1, IM_COL32(18, 22, 34, 255), IM_COL32(18, 22, 34, 255), IM_COL32(40, 46, 66, 255), IM_COL32(40, 46, 66, 255));
    const Vec3 origin = ps.transform ? ps.transform->Position() : Vec3::Zero;
    const float ang = pv.yaw * 0.0174532925f;
    const Vec3 target = origin + Vec3{0, 1.0f, 0};
    const Vec3 eye = target + Vec3{std::sin(ang) * 6.0f, 1.5f, std::cos(ang) * 6.0f};
    ParticleView view;
    view.vp = Mat4::Perspective(50.0f, w / h, 0.1f, 200.0f) * Mat4::LookAt(eye, target, Vec3{0, 1, 0});
    view.camPos = eye; view.x0 = p0.x; view.y0 = p0.y; view.w = w; view.h = h; view.maxRadius = 120.0f; view.frustumCull = false; view.useDepth = false; view.trueAdditive = true;
    // A little ground ring so the eye has something to turn against.
    for (int k = 0; k < 24; ++k) {
        const float a0 = k / 24.0f * 6.2831853f, a1 = (k + 1) / 24.0f * 6.2831853f;
        float sx0, sy0, sx1, sy1;
        auto proj = [&](const Vec3& p, float& sx, float& sy) {
            const Vec4 c = view.vp * Vec4{p, 1.0f};
            if (c.w <= 0.05f) return false;
            sx = p0.x + (c.x / c.w * 0.5f + 0.5f) * w; sy = p0.y + (0.5f - c.y / c.w * 0.5f) * h; return true; };
        if (proj(Vec3{origin.x + std::cos(a0) * 2.5f, origin.y, origin.z + std::sin(a0) * 2.5f}, sx0, sy0) &&
            proj(Vec3{origin.x + std::cos(a1) * 2.5f, origin.y, origin.z + std::sin(a1) * 2.5f}, sx1, sy1))
            dl->AddLine(ImVec2(sx0, sy0), ImVec2(sx1, sy1), IM_COL32(120, 130, 160, 110), 1.0f);
    }
    static std::vector<ParticleDrawItem> items;
    ParticleDrawStats st;
    BuildParticleDrawFor(ps, origin, view, items, &st);
    DrawParticleItemsImGui(dl, items, nullptr);
    dl->PopClipRect();
    dl->AddRect(p0, p1, IM_COL32(90, 100, 130, 255));
    ImGui::TextDisabled("%d particles alive (cap %d), %d shapes drawn", ps.AliveCount(), ps.SafeMax(), st.items);
}

bool DrawParticleInspectorExtras(okay::editor::EditorState& ed, ParticleSystem& ps, const ParticleCurveFn& curveEditor) {
    bool any = false;
    auto C = [&](bool c) { if (c) { ed.dirty = true; any = true; } return c; };

    if (ImGui::CollapsingHeader("Live preview##psx", ImGuiTreeNodeFlags_DefaultOpen)) DrawPreview(ed, ps);

    if (ImGui::CollapsingHeader("Emission extras##psx")) {
        C(ImGui::DragFloat("Per distance##psx", &ps.emitPerDistance, 0.05f, 0.0f, 1000.0f));
        Tip("Particles for every unit the object moves. A dust trail behind a walker. 0 = off.");
        C(ImGui::Checkbox("Already running at start##psx", &ps.prewarm));
        Tip("The effect is fully going from the very first frame (a campfire that was always burning).");
        const char* caps[] = {"Skip new ones", "Replace old ones"};
        int cp = (int)ps.capPolicy;
        if (C(ImGui::Combo("When full##psx", &cp, caps, 2))) ps.capPolicy = (ParticleSystem::CapPolicy)cp;
        Tip("What happens when Max Particles is reached: skip the new ones, or make room by replacing an old one.");
        ImGui::TextDisabled("More bursts (a burst fires a number of particles at a time in each round)");
        ImGui::TextDisabled("Start values that change while the effect plays (1 = as set above)");
        {
            const char* names[] = {"Size changes over the round", "Speed changes over the round", "Lifetime changes over the round"};
            ParticleCurve* cv[] = {&ps.startSizeCurve, &ps.startSpeedCurve, &ps.startLifetimeCurve};
            for (int q = 0; q < 3; ++q) {
                ImGui::PushID(100 + q);
                bool on = cv[q]->Active();
                if (ImGui::Checkbox(names[q], &on)) { if (on) cv[q]->SetLinear(1.0f, 1.0f); else cv[q]->Clear(); ed.dirty = true; any = true; }
                if (on && curveEditor && curveEditor(q == 0 ? "psxssz" : (q == 1 ? "psxssp" : "psxslf"), *cv[q])) { ed.dirty = true; any = true; }
                ImGui::PopID();
            }
            if (cv[0]->Active() || cv[1]->Active() || cv[2]->Active()) {
                C(ImGui::DragFloat("Round length (s)##psx", &ps.startCurvePeriod, 0.05f, 0.01f, 10000.0f));
                Tip("How long one round of these curves takes. When the effect has a Duration, that is the round.");
            }
        }
        int remove = -1;
        for (int b = 0; b < ps.extraBurstCount; ++b) {
            ImGui::PushID(b);
            ImGui::SetNextItemWidth(90); C(ImGui::DragFloat("at##psxb", &ps.extraBursts[b].time, 0.02f, 0.0f, 1000.0f, "%.2f s"));
            ImGui::SameLine(); ImGui::SetNextItemWidth(90); C(ImGui::DragInt("count##psxb", &ps.extraBursts[b].count, 1.0f, 0, 100000));
            ImGui::SameLine(); if (ImGui::SmallButton("Remove##psxb")) remove = b;
            ImGui::PopID();
        }
        if (remove >= 0) { for (int b = remove; b + 1 < ps.extraBurstCount; ++b) ps.extraBursts[b] = ps.extraBursts[b + 1]; --ps.extraBurstCount; ed.dirty = true; any = true; }
        if (ps.extraBurstCount < ParticleSystem::kMaxBursts && ImGui::SmallButton("Add a burst##psx")) {
            ps.extraBursts[ps.extraBurstCount++] = {0.5f, 20}; ed.dirty = true; any = true; }
        if (ps.shape == ParticleSystem::Shape::Sphere || ps.shape == ParticleSystem::Shape::Hemisphere || ps.shape == ParticleSystem::Shape::Box) {
            C(ImGui::Checkbox("Only from the skin##psx", &ps.shapeSurfaceOnly));
            Tip("Start the particles on the outside of the shape instead of anywhere inside it.");
        }
        if (ps.shape == ParticleSystem::Shape::MeshSurface) ImGui::TextDisabled("Starts on the skin of this object's Mesh Renderer.");
    }

    if (ImGui::CollapsingHeader("Look extras##psx")) {
        const char* blends[] = {"Normal", "Glow (adds light)", "Premultiplied"};
        int bm = (int)ps.blendMode; if (C(ImGui::Combo("Blend##psx", &bm, blends, 3))) ps.blendMode = (ParticleSystem::BlendMode)bm;
        Tip("Normal covers what is behind. Glow adds light, good for fire and magic.");
        const char* sorts[] = {"Nearest in front", "Oldest behind", "Youngest behind", "Birth order"};
        int sm = (int)ps.sortMode; if (C(ImGui::Combo("Draw order##psx", &sm, sorts, 4))) ps.sortMode = (ParticleSystem::SortMode)sm;
        Tip("Which particle is drawn on top when they overlap.");
        C(ImGui::Checkbox("Different colours##psx", &ps.randomStartColor));
        Tip("Each particle starts with its own colour.");
        if (ps.randomStartColor) {
            C(ImGui::Checkbox("Any colour at all##psx", &ps.randomHue));
            if (!ps.randomHue) { float c2[4] = {ps.startColor2.r, ps.startColor2.g, ps.startColor2.b, ps.startColor2.a};
                if (C(ImGui::ColorEdit4("Second colour##psx", c2))) ps.startColor2 = {c2[0], c2[1], c2[2], c2[3]}; }
        }
        C(ImGui::DragFloat("Soft edge##psx", &ps.softDistance, 0.02f, 0.0f, 20.0f));
        Tip("Fade the particle out where it touches a solid surface, so it does not cut a hard line. 0 = off.");
        C(ImGui::Checkbox("Lit by the scene lights##psx", &ps.lit));
        Tip("Smoke and dust get darker at night and in shade. Leave off for glowing things.");
        ImGui::TextDisabled("Picture sheet (several pictures in one image, played like a flip-book)");
        C(ImGui::DragInt("Pictures across##psx", &ps.sheetColumns, 0.2f, 1, 64));
        C(ImGui::DragInt("Pictures down##psx", &ps.sheetRows, 0.2f, 1, 64));
        if (ps.sheetColumns > 1 || ps.sheetRows > 1) {
            const char* shm[] = {"Over its life", "A random picture", "Loop"};
            int sh = (int)ps.sheetMode; if (C(ImGui::Combo("Plays##psx", &sh, shm, 3))) ps.sheetMode = (ParticleSystem::SheetMode)sh;
            C(ImGui::DragFloat(ps.sheetMode == ParticleSystem::SheetMode::Loop ? "Pictures per second##psx" : "Times over its life##psx", &ps.sheetCycles, 0.05f, 0.0f, 1000.0f));
            C(ImGui::Checkbox("Fade between pictures##psx", &ps.sheetBlendFrames));
        }
        ImGui::TextDisabled("Ribbon trail behind each particle");
        C(ImGui::SliderInt("Trail length##psx", &ps.trailLength, 0, ParticleSystem::kMaxTrailPoints));
        Tip("How many points the ribbon has. 0 = no trail. Costs a little more for each particle.");
        if (ps.trailLength > 0) {
            C(ImGui::DragFloat("Trail spacing (s)##psx", &ps.trailInterval, 0.005f, 0.005f, 5.0f));
            C(ImGui::DragFloat("Trail width##psx", &ps.trailWidth, 0.02f, 0.0f, 10.0f));
            C(ImGui::SliderFloat("Trail fade##psx", &ps.trailFade, 0.0f, 1.0f));
        }
    }

    if (ImGui::CollapsingHeader("Motion extras##psx")) {
        C(ImGui::Checkbox("Spin changes over life##psx", &ps.rotationOverLife));
        if (ps.rotationOverLife) {
            if (!ps.rotationCurve.Active()) ps.rotationCurve.SetLinear(1.0f, 0.0f);
            if (curveEditor && curveEditor("psxrot", ps.rotationCurve)) { ed.dirty = true; any = true; }
        }
        float lv[3] = {ps.linearVelocity.x, ps.linearVelocity.y, ps.linearVelocity.z};
        if (C(ImGui::DragFloat3("Drift##psx", lv, 0.05f))) ps.linearVelocity = {lv[0], lv[1], lv[2]};
        Tip("A steady sideways or upward drift (units per second), on top of the start speed.");
        C(ImGui::DragFloat("Away from centre##psx", &ps.radialVelocity, 0.05f, -100.0f, 100.0f));
        Tip("Positive pushes particles away from the object, negative pulls them in.");
        float fv[3] = {ps.force.x, ps.force.y, ps.force.z};
        if (C(ImGui::DragFloat3("Push##psx", fv, 0.05f))) ps.force = {fv[0], fv[1], fv[2]};
        Tip("A steady push that keeps speeding the particles up, like a fan.");
        const char* names[] = {"Push strength over life", "Gravity over life", "Drift strength over life"};
        ParticleCurve* curves[] = {&ps.forceCurve, &ps.gravityCurve, &ps.velocityCurve};
        for (int k = 0; k < 3; ++k) {
            ImGui::PushID(k);
            bool on = curves[k]->Active();
            if (ImGui::Checkbox(names[k], &on)) { if (on) curves[k]->SetLinear(1.0f, 0.0f); else curves[k]->Clear(); ed.dirty = true; any = true; }
            if (on && curveEditor && curveEditor(k == 0 ? "psxfrc" : (k == 1 ? "psxgrv" : "psxvel"), *curves[k])) { ed.dirty = true; any = true; }
            ImGui::PopID();
        }
        C(ImGui::SliderInt("Noise layers##psx", &ps.noiseOctaves, 1, 3));
        Tip("More layers add finer swirls to the wobble (Noise Strength above sets how strong).");
    }

    if (ImGui::CollapsingHeader("Hitting things##psx")) {
        C(ImGui::Checkbox("Ends when it hits##psx", &ps.dieOnCollision));
        Tip("A particle disappears the first time it touches the ground or something solid.");
        C(ImGui::Checkbox("Hit things in the scene##psx", &ps.worldCollision));
        Tip("Bounce off walls and floors that have a collider. Needs a little more time for each particle.");
        C(ImGui::Checkbox("Hit 2D things in the scene##psx", &ps.worldCollision2D));
        Tip("For 2D games: bounce off sprites that have a 2D collider (box, circle, capsule, edge, polygon).");
        if (ps.worldCollision || ps.worldCollision2D) {
            C(ImGui::DragInt("Checks per frame##psx", &ps.collisionRaysPerFrame, 1.0f, 1, 100000));
            Tip("How many particles are checked each frame. Lower is faster but fast particles may slip through for a moment.");
            ImGui::TextDisabled("Bounce, Friction and Life Loss are in the Collision rows above.");
        }
    }

    if (ImGui::CollapsingHeader("Sub-effects##psx")) {
        ImGui::TextDisabled("A sub-effect is another Particle System on a child object. It fires where each particle is born, ends or hits something.");
        struct Row { const char* label; std::string* name; int* count; const char* childName; const char* tip; };
        const Row rows[] = {
            {"When born", &ps.subEmitterBirth, &ps.subBirthCount, "Birth effect", "A few particles where each one starts."},
            {"When it ends", &ps.subEmitterDeath, &ps.subDeathCount, "End effect", "A puff or sparks where each one ends."},
            {"When it hits", &ps.subEmitterCollision, &ps.subCollisionCount, "Hit effect", "A splash where each one hits something."}};
        for (const Row& r : rows) {
            ImGui::PushID(r.label);
            char buf[96]; std::strncpy(buf, r.name->c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
            ImGui::SetNextItemWidth(150);
            if (ImGui::InputText(r.label, buf, sizeof(buf))) { *r.name = buf; ed.dirty = true; any = true; }
            Tip(r.tip);
            ImGui::SameLine(); ImGui::SetNextItemWidth(70);
            C(ImGui::DragInt("count", r.count, 0.2f, 0, 1000));
            ImGui::SameLine();
            if (ImGui::SmallButton("Make one")) {
                GameObject* self = ps.gameObject;
                if (self) {
                    GameObject* kid = ed.CreateEmpty(r.childName);
                    kid->transform->SetParent(self->transform, false);
                    auto* kp = kid->AddComponent<ParticleSystem>();
                    kp->ApplyPreset(ParticleSystem::Preset::HitSpark);
                    kp->emissionRate = 0.0f; kp->burstCount = 0; kp->duration = 0.0f; kp->extraBurstCount = 0; kp->maxParticles = 400; kp->Awake();
                    *r.name = r.childName; ed.dirty = true; any = true;
                }
            }
            Tip("Creates a child object with a small spark effect and fills in its name here.");
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Speed and distance##psx")) {
        C(ImGui::DragFloat("Fewer from (m)##psx", &ps.lodNear, 0.5f, 0.0f, 100000.0f));
        Tip("From this distance away from the camera the effect makes fewer particles. 0 with 'to' = off.");
        C(ImGui::DragFloat("Fewest at (m)##psx", &ps.lodFar, 0.5f, 0.0f, 100000.0f));
        C(ImGui::SliderFloat("Fewest amount##psx", &ps.lodMinimum, 0.0f, 1.0f));
        C(ImGui::Checkbox("Keep going off screen##psx", &ps.simulateOffscreen));
        Tip("Off = the effect pauses while nobody can see it (saves time). On = it always keeps running.");
        C(ImGui::DragInt("Use helpers above##psx", &ps.parallelThreshold, 16.0f, 0, 1000000));
        Tip("With this many live particles or more the work is shared with the helper threads. 0 = never.");
        ImGui::TextDisabled("Pool memory: %.1f KB", ps.MemoryBytes() / 1024.0);
    }
    return any;
}

} // namespace oked
