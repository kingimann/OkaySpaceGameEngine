#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Modeling/Modifiers.hpp"
#include <memory>
#include <string>
#include <vector>

namespace okay {

/// A non-destructive modifier stack on a mesh object (Mirror, Array,
/// Subdivision Surface, Solidify, Bevel, Boolean, Decimate, Displace). The
/// MeshRenderer's `mesh` stays the editable base; the stack's result goes to
/// `MeshRenderer::modifiedMesh`, which is what gets drawn, collides and is
/// exported. The result is rebuilt whenever the base, a setting or a Boolean
/// target changes — in the editor every frame, in a game in Update.
class MeshModifiers : public Component {
public:
    std::vector<MeshModifier> modifiers;
    /// Runtime (not saved): the editor sets this while the object is in mesh
    /// edit mode, so modifiers with showInEditMode off are skipped.
    bool editCage = false;
    /// Runtime: per modifier, why it failed last time ("" = fine).
    std::vector<std::string> errors;

    void Start() override { Refresh(); }
    void Update(float) override { Refresh(); }
    void OnDestroy() override {
        if (auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr) mr->modifiedMesh.reset();
    }

    /// Rebuild the result if anything it depends on changed (or always when
    /// `force`). Returns true if it was rebuilt.
    bool Refresh(bool force = false) {
        auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
        if (!mr) return false;
        bool any = false;
        for (const auto& m : modifiers) if (m.enabled && (!editCage || m.showInEditMode)) { any = true; break; }
        if (!enabled || !any) {
            errors.assign(modifiers.size(), std::string());
            bool had = (bool)mr->modifiedMesh;
            mr->modifiedMesh.reset();
            m_hash = 0;
            return had;
        }
        ModifierContext ctx = Context();
        std::uint64_t h = ModifierInputHash(mr->mesh, modifiers, ctx);
        if (!force && h == m_hash && mr->modifiedMesh) return false;
        m_hash = h;
        // Re-evaluate into the same Mesh object: renderers cache GPU buffers per mesh
        // object, so a fresh allocation every change would pile up stale buffers.
        Mesh result = EvaluateModifiers(mr->mesh, modifiers, ctx, &errors);
        if (mr->modifiedMesh) *mr->modifiedMesh = std::move(result);
        else mr->modifiedMesh = std::make_shared<Mesh>(std::move(result));
        return true;
    }

    /// Bake modifier `index` into the base mesh on its own (like Blender's Apply)
    /// and remove it from the stack. False with the reason if it couldn't run.
    bool Apply(int index, std::string* error = nullptr) {
        auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
        if (!mr || index < 0 || index >= (int)modifiers.size()) return false;
        Mesh out;
        if (!ApplyModifier(mr->mesh, modifiers[index], Context(), out, error)) return false;
        mr->mesh = std::move(out);
        modifiers.erase(modifiers.begin() + index);
        Refresh(true);
        return true;
    }
    /// Bake the whole enabled stack into the base and clear it.
    void ApplyAll() {
        auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
        if (!mr) return;
        ModifierContext ctx = Context(); ctx.editMode = false;
        mr->mesh = EvaluateModifiers(mr->mesh, modifiers, ctx, &errors);
        modifiers.clear();
        Refresh(true);
    }
    void Move(int from, int to) {
        if (from < 0 || to < 0 || from >= (int)modifiers.size() || to >= (int)modifiers.size() || from == to) return;
        MeshModifier m = modifiers[from];
        modifiers.erase(modifiers.begin() + from);
        modifiers.insert(modifiers.begin() + to, m);
    }

private:
    std::uint64_t m_hash = 0;
    ModifierContext Context() const {
        ModifierContext c;
        c.scene = gameObject ? gameObject->scene() : nullptr;
        c.owner = gameObject;
        c.editMode = editCage;
        return c;
    }
};

/// Refresh every modifier stack in the scene, and drop results left behind by a
/// removed stack. The editor calls this each frame (components don't Update
/// while editing); a game's stacks refresh in Update.
inline void RefreshModifierStacks(Scene& scene) {
    for (const auto& go : scene.Objects()) {
        if (!go) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr) continue;
        if (auto* mm = go->GetComponent<MeshModifiers>()) mm->Refresh();
        else if (mr->modifiedMesh) mr->modifiedMesh.reset();
    }
}

} // namespace okay
