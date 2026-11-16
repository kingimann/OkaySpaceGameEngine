#pragma once
// Undo steps inside mesh edit mode, kept apart from the editor so they can be tested without a window.
// A step is a copy of just the edited mesh, its selection and the object's modifier list (not a text
// snapshot of the whole scene), so undo stays instant on big meshes and a modifier changed during an
// edit session is undone together with the mesh. Written from the plain behaviour (two stacks, a
// state swap on undo and redo); no outside code.
#include "okay/Render/Mesh.hpp"
#include "okay/Modeling/Modifiers.hpp"
#include <string>
#include <utility>
#include <vector>

namespace okay::modeling {

struct MeshUndoStep {
    Mesh mesh;
    std::vector<int> verts, faces;
    std::vector<std::pair<int, int>> edges;
    std::vector<MeshModifier> modifiers;   // the object's modifier list at that moment
    bool hasModifiers = false;             // false when the object has no modifier component
    std::string label;                     // what was done after this state (the Undo history list shows it)
};

/// The live state a step reads from and writes back to. `mods` is null when the object has no modifiers.
struct MeshUndoTarget {
    Mesh* mesh = nullptr;
    std::vector<int>* verts = nullptr;
    std::vector<int>* faces = nullptr;
    std::vector<std::pair<int, int>>* edges = nullptr;
    std::vector<MeshModifier>* mods = nullptr;
};

inline MeshUndoStep CaptureMeshUndo(const MeshUndoTarget& t) {
    MeshUndoStep s;
    if (t.mesh) s.mesh = *t.mesh;
    if (t.verts) s.verts = *t.verts;
    if (t.faces) s.faces = *t.faces;
    if (t.edges) s.edges = *t.edges;
    if (t.mods) { s.modifiers = *t.mods; s.hasModifiers = true; }
    return s;
}

inline void RestoreMeshUndo(const MeshUndoTarget& t, MeshUndoStep& s) {
    if (t.mesh) *t.mesh = std::move(s.mesh);
    if (t.verts) *t.verts = std::move(s.verts);
    if (t.faces) *t.faces = std::move(s.faces);
    if (t.edges) *t.edges = std::move(s.edges);
    if (t.mods && s.hasModifiers) *t.mods = std::move(s.modifiers);
}

/// Call before changing the mesh: one undo step, and the redo steps are dropped. Bounded: at most `maxSteps`
/// steps and about `maxVertices` stored vertices (the oldest go first, the newest always stays).
inline void PushMeshUndo(std::vector<MeshUndoStep>& undo, std::vector<MeshUndoStep>& redo, const MeshUndoTarget& t,
                         const std::string& label, std::size_t maxSteps = 100, std::size_t maxVertices = 4000000) {
    undo.push_back(CaptureMeshUndo(t));
    undo.back().label = label.empty() ? std::string("Edit") : label;
    redo.clear();
    std::size_t total = 0;
    for (const auto& s : undo) total += s.mesh.vertices.size();
    while (undo.size() > 1 && (undo.size() > maxSteps || total > maxVertices)) {
        total -= undo.front().mesh.vertices.size();
        undo.erase(undo.begin());
    }
}

/// Undo (or, with `redoDir`, redo) one step: the current state goes onto the other stack. False when there is nothing to do.
inline bool StepMeshUndo(std::vector<MeshUndoStep>& undo, std::vector<MeshUndoStep>& redo, const MeshUndoTarget& t, bool redoDir) {
    auto& from = redoDir ? redo : undo;
    auto& to = redoDir ? undo : redo;
    if (!t.mesh || from.empty()) return false;
    to.push_back(CaptureMeshUndo(t));
    to.back().label = from.back().label;
    RestoreMeshUndo(t, from.back());
    from.pop_back();
    return true;
}

} // namespace okay::modeling
