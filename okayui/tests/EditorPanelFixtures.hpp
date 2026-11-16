#pragma once
// Sample data for the Problems, Stats and History panels, shared by the OkayUI tests (test_okayui_editor_panels.cpp) and the
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

} // namespace panelfix
