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
        model.quality = 1; model.solverIterations = 10; model.autoContinuous = true;
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
                          sec("Render.Scene", 2.30, 2.90, 1, 1, 2.2, 4.0), sec("Render.Shadows", 0.60, 0.60, 1, 2, 0.6, 1.1),
                          sec("UI.Editor", 0.60, 0.60, 1, 1, 0.7, 1.4)};
        model.playing = true;
    }
    ProfilerData(const ProfilerData&) = delete;
};

} // namespace panelfix
