#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/VoxelDebris.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/VoxelAim.hpp"
#include "okay/Components/PlayerController.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Audio/AudioLoad.hpp"
#include <memory>
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Core/Profiler.hpp"
#include "okay/Core/FrameOverlay.hpp"
#include <chrono>
#include "okay/Render/Mesh.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/GameInput.hpp"
#include "okay/Core/Game.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace okay {

/// Runtime digging for a VoxelTerrain — carve real caves, tunnels and overhangs
/// (the smooth, non-Minecraft kind). Aim with the main camera and hold the button
/// to subtract material (Dig) or add it (Add); the marching-cubes surface re-skins
/// live each frame. A sphere marker shows where you're aiming.
///
/// Drop it on the VoxelTerrain object (it digs itself) or anywhere in the scene
/// (it finds the first VoxelTerrain). The main Camera supplies the aim ray.
class VoxelDigger : public Behaviour {
public:
    enum class Mode { Dig, Add, Smooth, Box };
    Mode  mode     = Mode::Dig;
    int   button   = 0;        ///< mouse button held for the primary action (mode); <0 = none
    /// Secondary button for the OPPOSITE action, so one digger both digs AND
    /// raises out of the box: left mouse digs, right mouse adds material back.
    int   addButton = 1;
    char  key      = 0;        ///< optional key that also acts (0 = none)
    float radius   = 2.5f;     ///< brush radius (world units)
    float strength = 8.0f;     ///< density change per second while held
    float range    = 80.0f;    ///< how far the aim ray reaches
    bool  showBrush  = true;
    // ---- Aim and controls (first AND third person; docs/voxel_sandbox_dig.md) ----
    /// How far from the CHARACTER (not the camera) ground can be dug or built on. Used when the scene has a player.
    float reach = 12.0f;
    /// With a player in the scene the aim ray starts where the view line passes the character (third person) and refuses
    /// hits inside the body or behind a wall. Off: the ray starts at the camera, as the old digger did.
    bool  playerAim = true;
    /// Read the named input actions Dig, Place, PickBlock, HotbarNext, HotbarPrev (rebindable) as well as the raw buttons.
    /// Once an action is rebound its default raw button stops working.
    bool  useActions = true;
    /// Number keys 1-9 pick the building material, the mouse wheel and the bumpers step through them (or change the brush size when the terrain has no materials).
    bool  hotbarKeys = true;
    bool  showProgress = true;     ///< the marker shrinks and turns white while a dig works through the ground, red when the tool is too weak
    Color brushColor = Color::FromBytes(255, 180, 70, 200);
    /// When you finish a dig (release the button), delete any disconnected solid
    /// "crumbs" smaller than this many voxels — the little floating bits a dig leaves
    /// that you can't reach. 0 disables the cleanup.
    int   removeFloaters = 8;


    // ---- Real-life digging (docs/terrain_water.md). All off by default: a digger from an older scene acts as before. ----
    /// What happens to the ground a brush takes away. Vanish = today's behaviour.
    enum class Collect { Vanish, Store, Debris, Both };
    enum class Preset { Custom, Realistic, Arcade, Creative };
    Collect collect = Collect::Vanish;
    bool  needsMaterial = false;   ///< building takes the material from the store
    bool  creative = false;        ///< unlimited store
    int   addMaterial = 0;         ///< material id placed by the build button (when the terrain has materials)
    float power = 1.0e9f;          ///< tool power: materials harder than this cannot be dug
    float hardnessScale = 1.0f;    ///< > 1 makes everything slower to dig
    VoxelTerrain::EditOptions::Shape shape = VoxelTerrain::EditOptions::Shape::Sphere;
    float depthLimit = -1.0f;      ///< metres a stroke may go below where it started (-1 = unlimited)
    float protectBelow = -1.0f;    ///< bedrock: nothing under this local height can be dug (-1 = off)
    bool  snapToGrid = false;      ///< build on voxel centres
    int   debrisMax = 48;          ///< pieces alive at once; more are put straight into the store
    float debrisLife = 20.0f;      ///< seconds a piece lies about
    bool  slump = false;           ///< loose ground slides to its angle of repose after a dig
    int   slumpPasses = 1;         ///< slump passes per frame (each moves at most 40 voxels): more = an avalanche that runs farther
    bool  collapse = false;        ///< pieces cut loose by a dig fall as debris instead of vanishing
    char  pourKey = 0;             ///< key that pours fluid at the aim point when the terrain has `fluidOn` (0 = none)
    int   pourCells = 30;          ///< cells of fluid per press
    int   caveInSpan = 0;          ///< cave-in rule (0 = off): roof more than this many voxels from a support comes down (VoxelTerrain::CaveIn); needs a stroke to end
    bool  giveItems = false;       ///< also hand `drop` items to the scene's Inventory when volume is stored
    /// Tools from the inventory (OFF by default). On: the tool power is the best `tools` entry whose item the scene's Inventory holds
    /// (`pickaxe:5` = an item called pickaxe digs materials up to hardness 5), or `bareHandsPower` with none. `power` is then ignored.
    bool  toolsFromInventory = false;
    std::string tools = "shovel:2,pickaxe:5,iron_pickaxe:7";
    float bareHandsPower = 0.8f;
    /// Building spends the material's `drop` items from the Inventory (`dropPerM3` items per cubic metre), and does nothing
    /// without them. Dug ground arrives as the same items when `giveItems` is on, so a stack can be picked up and placed again.
    bool  buildFromItems = false;
    float CurrentPower() const {
        if (!toolsFromInventory) return power;
        Scene* sc = GetScene(); Inventory* inv = sc ? sc->FindObjectOfType<Inventory>() : nullptr;
        float best = bareHandsPower;
        if (!inv) return best;
        std::size_t i = 0;
        while (i < tools.size()) {
            std::size_t e = tools.find(',', i); if (e == std::string::npos) e = tools.size();
            const std::string one = tools.substr(i, e - i); i = e + 1;
            const std::size_t c = one.find(':'); if (c == std::string::npos || c == 0) continue;
            const float pw = (float)std::atof(one.c_str() + c + 1);
            if (pw > best && inv->Has(one.substr(0, c))) best = pw;
        }
        return best;
    }
    std::string hudTarget;         ///< name of a Text object that lists the store
    int  lastDigMaterial = -1;     ///< the material most of the last dig stroke took (-1 = none yet); also picks the dust colour and sound
    std::vector<double> store;     ///< cubic metres per material id
    double Stored(int id) const { return id >= 0 && id < (int)store.size() ? store[(std::size_t)id] : 0.0; }
    double StoredByName(const std::string& n) const { VoxelTerrain* v = FindVoxel(); return v ? Stored(v->MaterialIndex(n)) : 0.0; }
    void Deposit(int id, double vol) {
        if (id < 0) id = 0;
        if (store.size() <= (std::size_t)id) store.resize((std::size_t)id + 1, 0.0);
        store[(std::size_t)id] += vol; hudDirty_ = true;
        if (giveItems) if (VoxelTerrain* v = FindVoxel()) if (id < (int)v->materials.size() && !v->materials[(std::size_t)id].drop.empty())
            if (Scene* sc = GetScene()) if (auto* inv = sc->FindObjectOfType<Inventory>()) {
                const auto& m = v->materials[(std::size_t)id];
                itemFrac_[id] += vol * m.dropPerM3;
                const int whole = (int)itemFrac_[id];
                if (whole > 0) { inv->AddPartial(m.drop, whole); itemFrac_[id] -= whole; }
            }
    }
    /// Set the options together: realistic = collect into the store and as debris, hardness and tool power matter,
    /// building needs material, ground slumps and collapses; arcade = ground vanishes but builds freely; creative =
    /// everything unlimited.
    void ApplyPreset(Preset p) {
        collect = Collect::Vanish; needsMaterial = creative = slump = collapse = false; hardnessScale = 1.0f; power = 1.0e9f;
        if (p == Preset::Realistic) { collect = Collect::Both; needsMaterial = true; slump = true; collapse = true; hardnessScale = 1.0f; power = 4.0f; }
        else if (p == Preset::Creative) { creative = true; collect = Collect::Store; }
        else if (p == Preset::Arcade) { collect = Collect::Vanish; }
    }
    /// Undo the last stroke (Ctrl+Z while playing does this).
    bool UndoLastStroke() { VoxelTerrain* v = FindVoxel(); if (!v || !v->UndoStroke()) return false; v->Apply(); return true; }
    bool UsesRealLife(const VoxelTerrain& v) const {
        return v.UseMaterials() || collect != Collect::Vanish || needsMaterial || creative || power < 1.0e8f || toolsFromInventory || buildFromItems || hardnessScale != 1.0f ||
               shape != VoxelTerrain::EditOptions::Shape::Sphere || depthLimit > 0.0f || protectBelow >= 0.0f || snapToGrid || slump || collapse || caveInSpan > 0;
    }

    VoxelTerrain* FindVoxel() const {
        if (gameObject)
            if (auto* v = gameObject->GetComponent<VoxelTerrain>()) return v;
        // Not on this object: remember which object had one, so a scene is not searched every frame.
        if (voxOwner_)
            if (GameObject* g = GameObject::FromInstanceId(voxOwner_))
                if (auto* v = g->GetComponent<VoxelTerrain>()) return v;
        Scene* s = GetScene();
        VoxelTerrain* v = s ? s->FindObjectOfType<VoxelTerrain>() : nullptr;
        voxOwner_ = (v && v->gameObject) ? v->gameObject->InstanceId() : 0;
        return v;
    }

    /// The camera the aim ray starts from: the scene's main camera when there is one, otherwise the first
    /// camera found (the old rule, which could pick a minimap or security camera that came first).
    Camera* AimCamera() const {
        Scene* s = GetScene();
        if (!s) return nullptr;
        if (s->mainCamera && s->mainCamera->gameObject && s->mainCamera->gameObject->transform) return s->mainCamera;
        return s->FindObjectOfType<Camera>();
    }

    /// The character the aim is measured from: the player the camera belongs to (first person: its parent; third person: the
    /// camera is a child of the player too), otherwise the scene's Player Controller. nullptr when there is none.
    Transform* FindBody(Camera* cam) const {
        auto isBody = [](GameObject* g) {
            return g && (g->GetComponent<PlayerController>() || g->GetComponent<FirstPersonController>() || g->GetComponent<ThirdPersonController>());
        };
        if (cam && cam->gameObject && cam->gameObject->transform)
            for (Transform* t = cam->gameObject->transform; t; t = t->Parent()) if (isBody(t->gameObject)) return t;
        Scene* sc = GetScene();
        if (!sc) return nullptr;
        if (auto* pc = sc->FindObjectOfType<PlayerController>()) return pc->gameObject ? pc->gameObject->transform : nullptr;
        if (auto* fp = sc->FindObjectOfType<FirstPersonController>()) return fp->gameObject ? fp->gameObject->transform : nullptr;
        if (auto* tp = sc->FindObjectOfType<ThirdPersonController>()) return tp->gameObject ? tp->gameObject->transform : nullptr;
        return nullptr;
    }

    /// The aim input for the current camera and player (what AimAtVoxel marches). Public so tests and tools can look at it.
    bool BuildAim(VoxelAimInput& in, bool place) const {
        Camera* cam = AimCamera();
        if (!cam || !cam->gameObject || !cam->gameObject->transform) return false;
        Transform* ct = cam->gameObject->transform;
        in.camPos = ct->Position();
        in.camDir = ct->Forward() * -1.0f;   // aim is -Forward (see BlockBuilder)
        in.range = range; in.reach = reach; in.place = place;
        if (playerAim) if (Transform* body = FindBody(cam)) { in.hasBody = true; in.feet = body->Position(); }
        return true;
    }

    /// March the camera aim ray and return the local-space point where it first enters solid voxel (Dig) or the last air
    /// point before it (`place`). The ray goes through the screen centre, in first and in third person; see VoxelAim.hpp for
    /// the character rules (reach from the character, not inside the body, not through a wall).
    bool AimAtVoxel(VoxelTerrain* vox, Vec3& outLocal) const { return AimAtVoxel(vox, outLocal, mode == Mode::Add); }
    bool AimAtVoxel(VoxelTerrain* vox, Vec3& outLocal, bool place) const {
        lastAim_ = VoxelAimHit{};
        if (!vox || !vox->gameObject || !vox->gameObject->transform) return false;
        VoxelAimInput in;
        if (!BuildAim(in, place)) return false;
        lastAim_ = VoxelAimFind(*vox, *vox->gameObject->transform, in);
        if (!lastAim_.hit) return false;
        outLocal = lastAim_.local;
        return true;
    }
    const VoxelAimHit& LastAim() const { return lastAim_; }
    /// 0..1: how much of the ground under the brush this stroke has taken away. 1 = it has all gone.
    float StrokeProgress() const { return progress_; }
    /// The aimed ground is harder than the tool can dig (materials on).
    bool AimBlocked() const { return aimBlocked_; }

    /// Input helpers (actions and raw buttons).
    static bool ActionRebound(const char* name) {
        const InputActions::Action* a = InputActions::Find(name);
        if (!a) return false;
        if (a->bindings.size() != a->defaults.size()) return true;
        for (std::size_t i = 0; i < a->bindings.size(); ++i) if (a->bindings[i].ToString() != a->defaults[i].ToString()) return true;
        return false;
    }
    bool DigHeld() const {
        const bool rebound = useActions && ActionRebound("Dig");
        if (button >= 0 && useActions && InputActions::Get("Dig")) return true;
        if (button >= 0 && !rebound && useActions && InputActions::Get("Fire")) return true;   // left mouse / RT (or the player's Fire keys)
        if (button >= 0 && Input::GetMouseButton(button) && !rebound) return true;
        return key && Input::GetKey(key);
    }
    bool PlaceHeld() const {
        if (addButton < 0) return false;
        const bool rebound = useActions && ActionRebound("Place");
        if (useActions && InputActions::Get("Place")) return true;
        if (useActions && !rebound && InputActions::Get("Aim")) return true;                   // right mouse / LT (or the player's Aim keys)
        return Input::GetMouseButton(addButton) && !rebound;
    }

    void Update(float dt) override {
        const auto digT0 = std::chrono::steady_clock::now();
        struct Rep { std::chrono::steady_clock::time_point t0; ~Rep() {
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            VoxelOverlayStats& o = FrameOverlay::Voxel(); o.digMs = o.digMs * 0.9 + ms * 0.1; } } rep{digT0};
        // Pause, an open bag/chest/menu, typing, or a click on UI: no aim marker and no digging.
        if (GameInputBlocked(GetScene())) { HideBrush(); DigSound(false); return; }
        VoxelTerrain* vox = FindVoxel();
        if (!vox) { HideBrush(); DigSound(false); return; }
        // Primary button does `mode`; the secondary button does the opposite — so
        // the player can both dig and raise (add) terrain with one component.
        bool primary = DigHeld();
        bool secondary = PlaceHeld();
        if (hotbarKeys) HandleHotbar(*vox);
        // Building aims at the air in front of the surface, digging at the surface itself.
        const bool wantAdd = (mode == Mode::Add && !(secondary && !primary)) || (mode == Mode::Dig && secondary && !primary);
        Vec3 local;
        bool aiming = AimAtVoxel(vox, local, wantAdd);
        if (aiming && useActions && InputActions::Down("PickBlock")) PickBlockAtAim(*vox);
        aimBlocked_ = aiming && AimTooHard(*vox);
        if (showBrush && aiming) ShowBrushAt(*vox, local);
        else HideBrush();
        bool acting = (primary || secondary) && dt > 0.0f && aiming;

        if (acting) {
            OKAY_PROFILE("Voxel.Edit");
            Mode m = mode;
            // The secondary button does the opposite carve/fill action; for Smooth it
            // stays a smooth (there's no "opposite" of smoothing).
            if (secondary && !primary && m != Mode::Smooth && m != Mode::Box) m = (mode == Mode::Add) ? Mode::Dig : Mode::Add;
            // A hitch (one very long frame) must not carve a crater: the brush counts at most a tenth of a second.
            float amt = strength * std::min(dt, 0.1f);
            bool changed = false;
            if (!m_wasActing) { vox->BeginStroke(); strokeY_ = local.y; strokeAt_ = local; solid0_ = SolidFraction(*vox, local); progress_ = 0.0f; }
            if ((m == Mode::Dig || m == Mode::Add) && UsesRealLife(*vox)) {
                changed = RealEdit(*vox, local, amt, m == Mode::Dig);
            }
            else if (m == Mode::Dig)    changed = vox->Dig(local, radius, amt);
            else if (m == Mode::Add)    changed = vox->Add(local, radius, amt);
            else if (m == Mode::Box) {
                Vec3 r{radius, radius, radius};
                changed = vox->DigBox({local.x - r.x, local.y - r.y, local.z - r.z},
                                      {local.x + r.x, local.y + r.y, local.z + r.z},
                                      (secondary && !primary) ? -amt : amt);   // 2nd button fills
            }
            else                        changed = vox->SmoothAt(local, radius, std::min(1.0f, amt));
            if (changed) {
                if (slump) for (int pass = 0; pass < slumpPasses; ++pass) if (vox->Slump(40) == 0) break;   // bounded passes near the edit
                vox->Apply();   // re-skin only when something actually moved
                if (vox->gameObject && vox->gameObject->transform) {
                    KickDebris(vox->gameObject->transform->TransformPoint(local), lastDigMaterial);
                }
            }
            if (m == Mode::Dig && solid0_ > 0.02f) { const float now = SolidFraction(*vox, strokeAt_);  /* measured where the stroke began: the aim point sinks as the ground goes */ progress_ = std::max(0.0f, std::min(1.0f, 1.0f - now / solid0_)); }
            DigSound(true);
        } else {
            progress_ = 0.0f;
            if (m_wasActing) {
                // Just released: sweep up any floating crumbs the dig left behind, then
                // re-skin once. (Done on release, not every frame, so it stays cheap, and only
                // around the cells this stroke touched.)
                if (removeFloaters > 1) {
                    OKAY_PROFILE("Voxel.Edit");
                    const int cleared = VoxelTerrain::OldRemeshActive() ? vox->RemoveFloaters(removeFloaters)
                                                                         : vox->RemoveFloatersEdited(removeFloaters);
                    if (cleared > 0) vox->Apply();
                }
                if ((collapse && vox->UseMaterials()) || caveInSpan > 0) CollapsePieces(*vox);
                vox->EndStroke();
            }
            if (Input::GetKey(Input::KeyFromName("ctrl")) && Input::GetKeyDown('z')) UndoLastStroke();
            if (pourKey && aiming && vox->fluidOn && Input::GetKeyDown(pourKey)) vox->PourFluid({local.x, local.y + vox->voxelSize * 1.5f, local.z}, pourCells);
            DigSound(false);
        }
        m_wasActing = acting;
        RefreshHud();
    }

    void OnDestroy() override {
        if (Scene* s = GetScene()) {
            if (brush_) s->Destroy(brush_);
            if (debris_) s->Destroy(debris_);
        }
        brush_ = nullptr;
        debris_ = nullptr;
    }

private:
    mutable VoxelAimHit lastAim_;
    float progress_ = 0.0f, solid0_ = 0.0f;
    Vec3  strokeAt_{0, 0, 0};
    bool  aimBlocked_ = false;

    /// Share of sample points inside the brush that are solid (a cheap 3x3x3 spread).
    float SolidFraction(const VoxelTerrain& vox, const Vec3& c) const {
        int solid = 0, n = 0;
        const float r = std::max(0.2f, radius * 0.45f);   // corners stay inside the brush
        for (int ix = -1; ix <= 1; ++ix) for (int iy = -1; iy <= 1; ++iy) for (int iz = -1; iz <= 1; ++iz) {
            ++n; if (vox.SampleDensity({c.x + ix * r, c.y + iy * r, c.z + iz * r}) > vox.iso) ++solid;
        }
        return n ? (float)solid / (float)n : 0.0f;
    }
    int CellMaterialAt(const VoxelTerrain& vox, const Vec3& local) const {
        if (!vox.UseMaterials()) return 0;
        auto cl = [](int v, int hi) { return std::max(0, std::min(hi - 1, v)); };
        const int x = cl((int)std::lround((local.x + vox.HalfX()) / vox.voxelSize), vox.nx);
        const int y = cl((int)std::lround(local.y / vox.voxelSize), vox.ny);
        const int z = cl((int)std::lround((local.z + vox.HalfZ()) / vox.voxelSize), vox.nz);
        return vox.MaterialAt(x, y, z);
    }
    /// The material under the aim is harder than the tool's power (materials and tool power matter only when UsesRealLife).
    bool AimTooHard(const VoxelTerrain& vox) const {
        if (!vox.UseMaterials() || !lastAim_.hit) return false;
        const int id = CellMaterialAt(vox, lastAim_.surfaceLocal);
        if (id < 0 || id >= (int)vox.materials.size()) return false;
        return vox.materials[(std::size_t)id].hardness > CurrentPower();
    }
    void PickBlockAtAim(VoxelTerrain& vox) {
        if (!vox.UseMaterials() || !lastAim_.hit) return;
        const int id = CellMaterialAt(vox, lastAim_.surfaceLocal);
        if (id >= 0 && id < (int)vox.materials.size()) addMaterial = id;
    }
    /// Number keys 1-9 pick the material to build with; the wheel and the bumpers step through them. With no materials they
    /// change the brush size instead.
    void HandleHotbar(const VoxelTerrain& vox) {
        const int nm = vox.UseMaterials() ? (int)vox.materials.size() : 0;
        for (int k = 1; k <= 9; ++k)
            if (Input::GetKeyDown((char)('0' + k)) && nm > 0 && k - 1 < nm) addMaterial = k - 1;
        int step = 0;
        const float w = Input::MouseWheel();
        if (w > 0.01f) step += 1; else if (w < -0.01f) step -= 1;
        if (useActions) { if (InputActions::Down("HotbarNext")) step += 1; if (InputActions::Down("HotbarPrev")) step -= 1; }
        if (step == 0) return;
        if (nm > 0) addMaterial = ((addMaterial + step) % nm + nm) % nm;
        else radius = std::max(1.0f, std::min(8.0f, radius + 0.5f * (float)step));
    }
    float strokeY_ = 0.0f;
    bool hudDirty_ = true;
    std::map<int, double> itemFrac_;
    double buildFrac_ = 0.0;
    std::map<std::string, std::shared_ptr<const AudioClip>> clipCache_;   // digSound files already loaded (or failed)

    void RefreshHud() {
        if (!hudDirty_ || hudTarget.empty()) return;
        Scene* sc = GetScene(); VoxelTerrain* v = FindVoxel();
        GameObject* g = sc ? sc->Find(hudTarget) : nullptr;
        auto* t = g ? g->GetComponent<TextRenderer>() : nullptr;
        if (!t || !v) return;
        std::string txt;
        for (std::size_t i = 0; i < store.size(); ++i) {
            if (store[i] < 0.005) continue;
            char buf[64]; std::snprintf(buf, sizeof(buf), "%s %.2f m3  ", i < v->materials.size() ? v->materials[i].name.c_str() : "?", store[i]);
            txt += buf;
        }
        t->text = txt.empty() ? "Nothing collected" : txt;
        hudDirty_ = false;
    }

    /// The Dig / Add brush with materials, hardness, shapes and conservation of volume.
    bool RealEdit(VoxelTerrain& vox, const Vec3& local, float amt, bool dig) {
        VoxelTerrain::EditOptions o;
        o.shape = shape; o.hardnessScale = hardnessScale; o.power = CurrentPower(); o.snap = snapToGrid;
        o.protectBelowY = protectBelow; if (depthLimit > 0.0f) { o.maxDepth = depthLimit; o.depthOriginY = strokeY_; }
        o.addMaterial = vox.UseMaterials() ? addMaterial : -1;
        if (dig) {
            const auto r = vox.Edit(local, radius, amt, o);
            if (!r.changed) return false;
            { double best = 0; for (std::size_t id = 0; id < r.removedBy.size(); ++id) if (r.removedBy[id] > best) { best = r.removedBy[id]; lastDigMaterial = (int)id; } }
            for (std::size_t id = 0; id < r.removedBy.size(); ++id) {
                const double vol = r.removedBy[id];
                if (vol <= 0.0) continue;
                CollectVolume(vox, (int)id, vol, local);
            }
            return true;
        }
        // building: from the store when it is needed, an unlimited supply otherwise
        const int mid = vox.UseMaterials() ? addMaterial : 0;
        if (needsMaterial && !creative && Stored(mid) <= 1e-6) return false;
        Inventory* binv = nullptr; const VoxelTerrain::VoxelMaterial* bm = nullptr;
        if (buildFromItems && vox.UseMaterials() && mid < (int)vox.materials.size() && !vox.materials[(std::size_t)mid].drop.empty()) {
            bm = &vox.materials[(std::size_t)mid];
            Scene* sc = GetScene(); binv = sc ? sc->FindObjectOfType<Inventory>() : nullptr;
            if (!binv || !binv->Has(bm->drop)) return false;               // nothing to build with
        }
        const auto r = vox.Edit(local, radius, -amt, o);
        if (!r.changed) return false;
        if (binv && bm) {
            buildFrac_ += r.added * bm->dropPerM3;
            const int whole = (int)buildFrac_;
            if (whole > 0) { binv->Remove(bm->drop, std::min(whole, binv->Count(bm->drop))); buildFrac_ -= whole; }
        }
        if (needsMaterial && !creative) { store[(std::size_t)mid] = std::max(0.0, store[(std::size_t)mid] - r.added); hudDirty_ = true; }
        return true;
    }

    void CollectVolume(VoxelTerrain& vox, int id, double vol, const Vec3& local) {
        if (collect == Collect::Vanish) return;
        Scene* sc = GetScene();
        const bool debris = (collect == Collect::Debris || collect == Collect::Both) && sc && vox.gameObject && vox.gameObject->transform;
        if (!debris) { Deposit(id, vol); return; }
        Transform* tt = vox.gameObject->transform;
        const int pieces = std::max(1, std::min(3, (int)std::ceil(vol / 0.08)));
        for (int i = 0; i < pieces; ++i) {
            const double pv = vol / pieces;
            if (VoxelDebris::Live() >= debrisMax) { Deposit(id, pv); continue; }   // no more bodies: keep it safe in the store
            SpawnDebris(vox, id, pv, tt->TransformPoint({local.x + 0.3f * (float)i, local.y + 0.6f, local.z}), nullptr, collect == Collect::Both);
        }
    }

    GameObject* SpawnDebris(VoxelTerrain& vox, int id, double vol, const Vec3& world, const Mesh* mesh, bool toStore) {
        Scene* sc = GetScene();
        GameObject* g = sc->CreateGameObject("Voxel Debris");
        g->transient = true;
        g->transform->localPosition = world;
        const Vec3 ts = vox.gameObject->transform->LossyScale();
        const float us = std::cbrt(std::fabs(ts.x * ts.y * ts.z));          // the terrain's scale (uniform part)
        g->transform->localRotation = vox.gameObject->transform->Rotation();
        const float side = (float)std::cbrt(std::max(vol, 1e-6)) * us;
        auto* mr = g->AddComponent<MeshRenderer>();
        mr->mesh = mesh ? *mesh : Mesh::Cube();
        if (!mesh) g->transform->localScale = {side, side, side};
        else g->transform->localScale = {us, us, us};
        mr->color = (vox.UseMaterials() && id < (int)vox.materials.size()) ? vox.materials[(std::size_t)id].color : vox.color;
        auto* bc = g->AddComponent<BoxCollider3D>();
        if (mesh) { Vec3 lo, hi; mesh->Bounds(lo, hi); bc->size = {std::max(0.1f, hi.x - lo.x), std::max(0.1f, hi.y - lo.y), std::max(0.1f, hi.z - lo.z)}; }
        auto* rb = g->AddComponent<Rigidbody3D>();
        const float dens = (vox.UseMaterials() && id < (int)vox.materials.size()) ? vox.materials[(std::size_t)id].density : 1500.0f;
        rb->mass = std::max(0.05f, (float)(vol * dens * 0.01));
        rb->bounciness = 0.15f;
        auto* d = g->AddComponent<VoxelDebris>();
        d->terrainId = vox.gameObject->InstanceId(); d->material = id; d->volume = vol; d->toStore = toStore; d->life = debrisLife;
        const std::uint64_t me = gameObject ? gameObject->InstanceId() : 0;
        d->onStore = [me](int m, double v) {
            if (GameObject* og = GameObject::FromInstanceId(me)) if (auto* dg = og->GetComponent<VoxelDigger>()) dg->Deposit(m, v);
        };
        return g;
    }

    void CollapsePieces(VoxelTerrain& vox) {
        std::vector<VoxelTerrain::Piece> pieces;
        if (collapse && vox.UseMaterials()) pieces = vox.ExtractFloaters(6);
        if (caveInSpan > 0) { auto more = vox.CaveIn(caveInSpan, 6); pieces.insert(pieces.end(), more.begin(), more.end()); }
        if (pieces.empty()) return;
        Scene* sc = GetScene();
        Transform* tt = vox.gameObject->transform;
        for (const auto& p : pieces) {
            if (!sc || p.cells.size() < 8 || VoxelDebris::Live() >= debrisMax) { Deposit(p.material, p.volume); continue; }   // crumbs and overflow go to the store
            const Mesh mesh = vox.PieceMesh(p);
            const Vec3 c{(p.lo.x + p.hi.x) * 0.5f, (p.lo.y + p.hi.y) * 0.5f, (p.lo.z + p.hi.z) * 0.5f};
            SpawnDebris(vox, p.material, p.volume, tt->TransformPoint(c), &mesh, false);
        }
        vox.Apply();
    }
    GameObject* brush_ = nullptr;
    GameObject* debris_ = nullptr;
    int  brushShapeShown_ = 0;   ///< which marker mesh the brush object has (a VoxelTerrain::EditOptions::Shape)
    bool m_wasActing = false;   ///< were we digging last frame? (to clean up crumbs on release)
    mutable std::uint64_t voxOwner_ = 0;   ///< InstanceId of the object that holds the VoxelTerrain (when it is not this one)
    bool soundHeld_ = false;
    bool soundStarted_ = false;

    void ShowBrushAt(VoxelTerrain& vox, const Vec3& local) {
        Scene* s = GetScene();
        if (!s || !vox.gameObject || !vox.gameObject->transform) return;
        GameObject* b = EnsureBrush(*s);
        if (!b || !b->transform) return;
        b->active = true;
        b->transform->localPosition = vox.gameObject->transform->TransformPoint(local);
        float dscale = radius * 2.0f;   // sphere mesh has radius 0.5
        if (showProgress) dscale *= 1.0f - 0.25f * progress_;
        // the marker follows the brush shape: a cube for Cube, a squashed disc for Plane, a tall can for Cylinder
        float sx = dscale, sy = dscale, sz = dscale;
        if (shape == VoxelTerrain::EditOptions::Shape::Plane) sy = dscale / 3.0f;
        else if (shape == VoxelTerrain::EditOptions::Shape::Cylinder) sy = dscale;
        b->transform->localScale = {sx, sy, sz};
        if (auto* mr = b->GetComponent<MeshRenderer>()) {
            mr->color = brushColor;
            if (showProgress) {
                if (aimBlocked_) mr->color = Color::FromBytes(230, 50, 40, 220);
                else if (progress_ > 0.0f) mr->color = Color{brushColor.r + (1.0f - brushColor.r) * progress_, brushColor.g + (1.0f - brushColor.g) * progress_, brushColor.b + (1.0f - brushColor.b) * progress_, brushColor.a};
            }
            if (brushShapeShown_ != (int)shape) {
                brushShapeShown_ = (int)shape;
                mr->mesh = shape == VoxelTerrain::EditOptions::Shape::Cube ? Mesh::Cube() : (shape == VoxelTerrain::EditOptions::Shape::Cylinder ? Mesh::Cylinder(0.5f, 1.0f, 16) : Mesh::Sphere(0.5f, 8, 12));
            }
        }
    }
    void HideBrush() { if (brush_) brush_->active = false; }

    GameObject* EnsureBrush(Scene& s) {
        if (brush_) return brush_;
        GameObject* b = s.CreateGameObject("VoxelBrush");
        if (!b) return nullptr;
        b->tag = "VoxelBrush";
        auto* mr = b->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Sphere(0.5f, 8, 12);
        mr->wireframe = true;          // hollow outline so you see through it
        mr->unlit = true;
        mr->shader = MeshRenderer::Shader::Unlit;
        mr->color = brushColor;
        brush_ = b;
        return b;
    }

    void KickDebris(const Vec3& world, int mat = -1) {
        Scene* s = GetScene();
        if (!s) return;
        if (!debris_) {
            debris_ = s->CreateGameObject("VoxelDebris");
            if (!debris_) return;
            debris_->tag = "VoxelDebris";
            auto* ps = debris_->AddComponent<ParticleSystem>();
            ps->playing = false;
            ps->emissionRate = 0.0f;
            ps->maxParticles = 48;
            ps->startLifetime = 0.45f;
            ps->startLifetimeRandom = 0.12f;
            ps->startSize = 0.14f;
            ps->startSizeRandom = 0.06f;
            ps->startVelocity = {0.0f, 2.2f, 0.0f};
            ps->shape = ParticleSystem::Shape::Sphere;
            ps->gravity = {0.0f, -12.0f, 0.0f};
            ps->startColor = Color::FromBytes(130, 104, 78);
            ps->fadeOverLife = true;
            ps->sizeOverLife = true;
            ps->endSize = 0.04f;
        }
        if (!debris_->transform) return;
        debris_->active = true;
        debris_->transform->localPosition = world;
        if (auto* ps = debris_->GetComponent<ParticleSystem>()) {
            ps->shapeRadius = std::max(0.2f, radius * 0.3f);
            if (mat >= 0) if (VoxelTerrain* vt = FindVoxel()) if (vt->UseMaterials() && mat < (int)vt->materials.size()) ps->startColor = vt->materials[(std::size_t)mat].particle;   // dust of the dug material
            ps->Emit(4);
        }
    }

    /// Dig sound hook: an AudioSource on this object plays while the button is
    /// held on the surface and stops on release. No clip is bundled.
    void DigSound(bool acting) {
        AudioSource* au = gameObject ? gameObject->GetComponent<AudioSource>() : nullptr;
        if (!au) { soundHeld_ = false; return; }
        if (acting && lastDigMaterial >= 0) {      // the dug material's own clip, loaded the first time it is used
            if (VoxelTerrain* vt = FindVoxel()) if (vt->UseMaterials() && lastDigMaterial < (int)vt->materials.size()) {
                const std::string& want = vt->materials[(std::size_t)lastDigMaterial].digSound;
                if (!want.empty() && au->clipPath != want) {
                    if (soundHeld_ && soundStarted_) au->Stop();
                    auto it = clipCache_.find(want);
                    if (it == clipCache_.end()) it = clipCache_.emplace(want, LoadAudioFile(want)).first;   // an empty pointer = tried, failed: not retried every frame
                    au->clipPath = want;
                    if (it->second) { au->clip = *it->second; soundHeld_ = false; }
                    else { au->clip = AudioClip(); soundHeld_ = false; }
                }
            }
        }
        if (acting) {
            if (!soundHeld_) {
                soundStarted_ = !au->IsPlaying();
                if (soundStarted_) au->Play();
                soundHeld_ = true;
            } else if (soundStarted_ && !au->IsPlaying()) {
                au->Play();
            }
        } else if (soundHeld_) {
            if (soundStarted_) au->Stop();
            soundHeld_ = false;
        }
    }
};

} // namespace okay
