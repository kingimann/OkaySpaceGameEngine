#pragma once
// Sample data for the Problems, Stats, History, Tags and Layers, Physics and Profiler panels, shared by the OkayUI tests (test_okayui_editor_panels.cpp) and the
// side-by-side comparison with the Dear ImGui versions (tests/test_okayui_editor_panels_imgui.cpp). Made up for the tests; it
// looks like what the editor shows for a small scene with a few mistakes in it.
#include "EditorPanelModels.hpp"

namespace panelfix {

using namespace okaypanels;

inline ProblemRow Row(std::uint64_t id, int sev, const char* code, const char* msg, const char* hint, const char* group, bool fix,
                      const char* guid = "") {
    ProblemRow r;
    r.id = id; r.severity = sev; r.code = code; r.message = msg; r.hint = hint; r.group = group; r.objectPath = group;
    r.objectGuid = guid; r.canFix = fix;
    return r;
}

inline ProblemsModel Problems() {
    ProblemsModel m;
    m.rows = {
        Row(1, ProblemWarning, "follow.missing_target", "Follow target 'Nope' is not in the scene", "Pick an object that exists, or clear the field.", "Chaser", true, "g-chaser"),
        Row(2, ProblemError, "sprite.missing_texture", "Texture Assets/missing_crate.png is missing", "Re-import the file or pick another texture.", "Crate", true, "g-crate"),
        Row(3, ProblemError, "player.missing_collider", "Character controller has no collider", "Add a CapsuleCollider3D so the player can stand on things.", "Player", true, "g-player"),
        Row(4, ProblemInfo, "scene.no_camera", "The scene has no main camera", "A default camera is used when you press Play.", "Scene", true),
        Row(5, ProblemWarning, "anim.empty_machine", "Animator has no states", "Open the Animator and add a state.", "Puppet", false, "g-puppet"),
        Row(6, ProblemError, "script.missing_file", "Script Assets/MissingScript.okay was not found", "Restore the file or remove the component.", "MissingScriptComponentFile", false, "g-missing"),
        Row(7, ProblemWarning, "nav.no_mesh", "NavMeshAgent has no baked NavMesh to walk on", "Bake the navigation in the Navigation window.", "Walker", false, "g-walker"),
        Row(8, ProblemError, "sprite.missing_texture", "Texture Assets/missing_crate.png is missing", "Re-import the file or pick another texture.", "Crate", true, "g-crate"),  // a duplicate: shown once
    };
    m.errors = 4; m.warnings = 3; m.infos = 1;
    return m;
}

inline FpsHistory Fps() {
    FpsHistory h;
    for (int i = 0; i < FpsHistory::kCount; ++i) {   // a steady 60 with a few dips, deterministic
        const int k = i % 23;
        h.Push(k == 7 ? 41.0f : k == 15 ? 52.0f : 58.0f + (float)(i % 5));
    }
    return h;
}

struct StatsData {
    FpsHistory hist = Fps();
    bool frustum = true, occlusion = false, staticBatch = true, dynamicBatch = false;
    StatsModel model;
    StatsData() {
        model.framerate = 59.6f;
        model.history = &hist;
        model.counters = {{"Objects", 148}, {"Triangles", 182340}, {"Meshes", 41}, {"Sprites", 12}, {"Lights", 3}, {"Colliders", 37},
                          {"Scripts", 9}, {"NPCs", 0}, {"Particles", 1250}, {"Drawn", 33}, {"Culled", 8}, {"Occluded", 0},
                          {"LOD culled", 0}, {"LOD0", 30}, {"LOD1+", 3}, {"Batches", 21}, {"Draws saved", 12}};
        model.frustumCulling = &frustum; model.occlusionCulling = &occlusion;
        model.staticBatching = &staticBatch; model.dynamicBatching = &dynamicBatch;
        model.gpuSkinning = true;
    }
    StatsData(const StatsData&) = delete;
};

inline HistoryModel History() {
    HistoryModel m;
    m.undoNames = {"Move Player", "Add Component BoxCollider3D", "Rename Crate", "Create Empty", "Delete Light"};
    m.redoNames = {"Scale Crate", "Paste"};
    m.undoDepth = 5; m.redoDepth = 2;
    return m;
}

inline TagsLayersModel TagsLayers() {
    TagsLayersModel m;
    m.strict = false;
    m.builtin = {"Untagged", "Player", "Enemy", "MainCamera", "Respawn", "Finish", "EditorOnly"};
    m.custom = {{"Pickup", 4}, {"Hazard", 2}, {"Checkpoint", 1}, {"NPC", 0}};
    m.unlisted = {"Boss", "Enmey"};
    m.layers.resize(kLayerCount);
    const char* defaults[kLayerCount] = {"Default", "TransparentFX", "Ignore Raycast", "", "Water", "UI"};
    for (int i = 0; i < kLayerCount; ++i) { m.layers[(std::size_t)i].defaultName = defaults[i] ? defaults[i] : ""; m.layers[(std::size_t)i].name = m.layers[(std::size_t)i].defaultName; }
    m.layers[8].name = "Ground"; m.layers[9].name = "Pickups"; m.layers[4].name = "Lakes";   // two new names and a renamed built-in one
    return m;
}

struct PhysicsData {
    float g2[2] = {0.0f, -9.81f};
    float g3[3] = {0.0f, -9.81f, 0.0f};
    float hz = 50.0f;
    bool smooth = true;
    PhysicsSettingsModel model;
    PhysicsData() {
        model.gravity2D = g2; model.gravity3D = g3; model.stepsPerSecond = &hz; model.smooth = &smooth;
        model.solverIterations = 10;
    }
    PhysicsData(const PhysicsData&) = delete;
};

struct ProfilerData {
    bool paused = false;
    int selected = -1;
    ProfilerModel model;
    ProfilerData() {
        model.paused = &paused; model.selected = &selected;
        for (int i = 0; i < 240; ++i) {   // a steady frame with a few spikes, deterministic
            const int k = i % 37;
            model.total.push_back(k == 11 ? 24.0f : k == 29 ? 19.5f : 9.0f + (float)(i % 7) * 0.4f);
            model.gpu.push_back(5.5f + (float)(i % 5) * 0.3f);
        }
        model.lastFrameMs = 10.2; model.avg = 10.4; model.peak = 24.0;
        model.frameMs = 10.2; model.cpuMs = 7.9; model.gpuMs = 5.8;
        model.meshes = 41; model.triangles = 182340; model.particles = 1250;
        model.scriptsMs = 2.6; model.physicsMs = 1.4; model.renderMs = 2.9;
        auto sec = [](const char* n, double self, double incl, int calls, int depth, double avg, double mx) {
            ProfilerSection s; s.name = n; s.self = self; s.inclusive = incl; s.calls = calls; s.depth = depth; s.min = self * 0.8; s.avg = avg; s.max = mx; return s;
        };
        model.sections = {sec("Frame", 0.40, 7.90, 1, 0, 0.42, 0.9), sec("Scripts.Update", 2.10, 2.60, 1, 1, 2.0, 3.1),
                          sec("Scripts.Coroutines", 0.50, 0.50, 3, 2, 0.4, 0.8), sec("Physics.Step", 1.40, 1.40, 2, 1, 1.3, 2.2),
                          sec("Render.Scene", 2.50, 3.10, 1, 1, 2.2, 4.0), sec("Render.Shadows", 0.60, 0.60, 1, 2, 0.6, 1.1),
                          sec("UI.Editor", 0.60, 0.60, 1, 1, 0.7, 1.4)};
        model.playing = true;
    }
    ProfilerData(const ProfilerData&) = delete;
};

// The import settings of a model with calculated normals (the most rows), unchanged since the last Apply; a texture's and a sound's.
struct ImportData {
    ImportFields edit;
    ImportSettingsModel model;
    ImportSettingsState state;
    explicit ImportData(int kind = ImportModel) {
        edit.kind = kind;
        if (kind == ImportModel) { edit.scale = 0.01f; edit.normals = 1; edit.smoothingAngle = 60.0f; edit.importAnimations = true; }
        if (kind == ImportTexture) {   // an anisotropic, compressed colour picture, loaded
            edit.maxSize = 1024; edit.filter = 2; edit.anisotropy = 8; edit.mipmaps = true; edit.mipFilter = 1;
            edit.compress = true; edit.compression = 0;
            model.loadedNote = "Loaded: 1024 x 1024, 11 mip levels, 1.33 MB, 1 handle";
        }
        if (kind == ImportAudio) { edit.loadMode = 1; edit.forceMono = false; }
        model.edit = &edit; model.original = edit;
    }
    ImportData(const ImportData&) = delete;
};

// Post-processing with bloom, colour grading, a vignette and SSAO on (most of the rows), depth of field off.
struct PostData {
    PostValues v;
    PostProcessModel model;
    PostData() {
        v.filmic = true; v.exposure = 1.25f; v.tonemap = 1;
        v.bloom = true; v.bloomThreshold = 1.0f; v.bloomIntensity = 0.8f; v.bloomRadius = 1.2f; v.bloomSoftness = 0.5f;
        v.grading = true; v.temperature = 0.15f; v.tint = -0.05f; v.saturation = 1.1f; v.contrast = 1.05f;
        v.lift[0] = 0.02f; v.gamma[1] = -0.1f; v.gain[2] = 0.05f;
        v.vignette = 0.25f; v.vignetteSmooth = 0.6f; v.fxaa = true;
        v.ssao = true; v.ssaoRadius = 0.5f; v.ssaoIntensity = 1.0f; v.ssaoPower = 1.5f;
        model.v = &v;
        model.presets = {"Default", "Sunset", "Night", "Foggy morning", "Storm"};
        model.chainActive = true;
        model.ssaoNote = "Ambient occlusion is not drawn on the software renderer.";
    }
    PostData(const PostData&) = delete;
};

struct PlayerData {
    char company[kProjectTextSize] = "Okay Games";
    char version[kProjectTextSize] = "1.4.2";
    int width = 1280, height = 720;
    PlayerSettingsModel model;
    PlayerData() { model.company = company; model.version = version; model.width = &width; model.height = &height; }
    PlayerData(const PlayerData&) = delete;
};

struct RenderingData {
    float ambient = 0.35f;
    bool skybox = true;
    RenderingSettingsModel model;
    RenderingData() { model.ambient = &ambient; model.skybox = &skybox; }
    RenderingData(const RenderingData&) = delete;
};

// The loading screen with a logo and the custom accent on (every row).
struct LoadingData {
    char logo[260] = "Assets/Textures/Logo.png";
    bool accentOverride = true, showBar = true;
    float accent[3] = {0.235f, 0.475f, 0.800f};
    float bgTop[3] = {0.094f, 0.094f, 0.106f}, bgBottom[3] = {0.051f, 0.051f, 0.059f};
    LoadingScreenModel model;
    LoadingData() {
        model.logo = logo; model.logoSize = sizeof logo;
        model.accentOverride = &accentOverride; model.accent = accent;
        model.bgTop = bgTop; model.bgBottom = bgBottom; model.showBar = &showBar;
    }
    LoadingData(const LoadingData&) = delete;
};

// The Net panel: the simulator on with some delay, three traffic rows.
struct NetData {
    bool enabled = true;
    float latency = 80.0f, jitter = 10.0f, loss = 0.02f, duplicate = 0.0f;
    NetSimModel model;
    NetData() {
        model.enabled = &enabled; model.latencyMs = &latency; model.jitterMs = &jitter; model.loss = &loss; model.duplicate = &duplicate;
        model.wireInPerSec = 5324.8f; model.wireOutPerSec = 2150.4f;
        model.rows = {{"player:1", 30.0f, 1840.0f}, {"rpc:Fire", 4.5f, 320.0f}, {"chat", 0.5f, 48.0f}};
    }
    NetData(const NetData&) = delete;
};

// Find in Files after a search for "health".
inline FindInFilesModel FindHits() {
    FindInFilesModel m;
    m.ran = true;
    m.hits = {{"Assets/Scripts/Player.okay", 12, "var health = 100"},
              {"Assets/Scripts/Enemy.lua", 40, "if health <= 0 then die() end"},
              {"Assets\\UI\\Hud.cs", 7, "label.text = $\"HP {health}\";"}};
    return m;
}

// A .okaydata file with three fields.
struct DataAssetData {
    std::vector<std::pair<std::string, std::string>> fields = {{"maxHealth", "100"}, {"speed", "4.5"}, {"title", "Knight"}};
    DataAssetModel model;
    DataAssetData() { model.path = "Assets/Data/knight.okaydata"; model.fields = &fields; }
    DataAssetData(const DataAssetData&) = delete;
};

// The Plugin Manager with three plugins: one on with a window, one with an error, one for all projects.
inline PluginManagerModel Plugins() {
    PluginManagerModel m;
    PluginRow a; a.name = "Batch Rename"; a.description = "Rename the selection with a pattern."; a.enabled = true; a.hasWindow = true;
    PluginRow b; b.name = "Level Stats"; b.error = "line 12: unknown function 'editor_count'";
    PluginRow c; c.name = "Snap All"; c.description = "Snap every object to the grid."; c.enabled = true; c.user = true;
    m.rows = {a, b, c};
    return m;
}


// Map Humanoid Bones for a rig with numbered bones: three picks made by hand, the rest Auto.
struct BoneMapData {
    std::string picks[kBoneSlotCount];
    BoneMapModel model;
    BoneMapData() {
        model.hasModel = true;
        model.modelName = "Robot_rig";
        model.bones = {"Bone.000", "Bone.001", "Bone.002", "Bone.003", "joint12", "joint13", "Head_end"};
        for (int b = 0; b < kBoneSlotCount; ++b) model.autoName[b] = "none";
        model.autoName[0] = "Bone.000"; model.autoName[2] = "Head_end";
        picks[1] = "Bone.001"; picks[3] = "joint12"; picks[4] = "-";
        model.picks = picks;
        model.humanoid = false; model.found = 6; model.why = "no left thigh";
    }
    BoneMapData(const BoneMapData&) = delete;
};

// The Save Manager on a playtest's save with five keys of each type.
inline SaveManagerModel Saves() {
    SaveManagerModel m;
    m.files = {"save.okaysave", "Assets/Saves/slot1.okaysave", "Assets/Saves/slot2.okaysave"};
    m.selected = m.files[1];
    std::vector<std::pair<std::string, std::string>> raw = {{"player.name", "s:Ada"}, {"coins", "i:120"}, {"music", "b:1"},
                                                            {"spawn", "v:1,0,4.5"}, {"health", "f:87.5"}};
    m.rows = SaveKeyRows(raw);
    m.dirty = true;
    return m;
}

// A red, slightly shiny material.
inline MaterialModel Material() {
    MaterialModel m;
    m.path = "Assets/Materials/red_paint.okaymat";
    m.color[0] = 0.85f; m.color[1] = 0.20f; m.color[2] = 0.18f; m.color[3] = 1.0f;
    m.emissive[0] = 0.10f;
    m.specular = 0.4f; m.shininess = 48.0f;
    m.texture = "Assets/Textures/paint.png";
    m.tiling[0] = 2.0f; m.tiling[1] = 1.0f;
    return m;
}

// The frame-time overlay after 120 steady frames with a few slow ones.
inline PerfOverlayModel Perf() {
    PerfOverlayModel m;
    m.fps = 142.0; m.avgMs = 7.04; m.p95Ms = 8.9; m.worstMs = 14.2; m.lowFps = 74.0;
    m.workMs = 4.1; m.sleepMs = 2.9;
    for (int i = 0; i < 120; ++i) m.plot.push_back(6.5f + (float)((i * 7) % 11) * 0.12f + (i % 37 == 0 ? 6.0f : 0.0f));
    m.breakFrames = 30;
    m.sections = PerfSections({"Scene view", "UI", "Sleep", "Scripts", "Physics"}, {2.4, 1.1, 2.9, 0.55, 0.0}, 7.04, 0);
    m.renderer = "OpenGL 3.3";
    m.sceneW = 1280; m.sceneH = 720; m.scale = 1.0f;
    return m;
}

// The Input panel while the game plays: two keys down, a controller, one player, three events, two actions.
inline InputPanelModel InputPanel() {
    InputPanelModel m;
    m.playing = true;
    m.snapshot = {"Keys down: W D Space", "Mouse 412, 300  buttons: none  wheel 0", "Controller 1: left stick -0.55, 0.35  A"};
    m.controllers = {"Controller 1: DualSense (054c:0ce6), mapped"};
    m.players = {{1, {"Move  Vector2  -0.55, 0.35  performed  stick:left", "Jump  Button  DOWN  started  pad:a"}}};
    m.events = {"key down Space", "pad 1 A down", "key down D"};
    m.recordStatus = "Kept: 0 frames";
    m.actions = {"Move", "Look", "Jump"};
    m.direction = true;
    m.value = 0.65f;
    m.shape = [](float in, const InputResponse& r) {
        const float d = in <= r.deadzone ? 0.0f : (in - r.deadzone) / (1.0f - r.deadzone);
        return std::pow(d, r.curve) * r.sensitivity;
    };
    return m;
}

} // namespace panelfix
