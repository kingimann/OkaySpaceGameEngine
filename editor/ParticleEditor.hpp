#pragma once
// The particle editor (ParticleEditor.cpp): drawing the shared particle list with ImGui, the one-click presets (GameObject >
// Effects, Add Component, the Scene view's right-click menu) and the Inspector rows the older Particle System inspector does not
// have, with the live preview (play / pause / restart / step, an orbiting mini view). docs/particles_upgrade.md.
#include "EditorState.hpp"
#include "okay/Render/ParticleDraw.hpp"
#include "imgui.h"
#include <functional>
#include <string>
#include <vector>

namespace oked {

/// Draw the list built by okay::BuildParticleDraw. `tex` turns a texture path into an ImGui texture (0 = none).
void DrawParticleItemsImGui(ImDrawList* dl, const std::vector<okay::ParticleDrawItem>& items,
                            const std::function<ImTextureID(const std::string&)>& tex);

/// GameObject > Effects > "Particle Effects": a ready-made object for each preset. Sets `created` when one was made.
void DrawParticlePresetMenu(okay::editor::EditorState& ed, bool& created);
/// The object for preset number `index` (the Scene view's right-click menu); null when the index is out of range.
okay::GameObject* CreateParticlePresetObject(okay::editor::EditorState& ed, int index);
/// Add Component > "Particle effect presets": adds a Particle System (or reuses the one there) with the look applied.
/// True when something changed.
bool DrawParticlePresetAddComponent(okay::editor::EditorState& ed, okay::GameObject* go);

/// Draws a curve editor and returns true when it changed the curve (the Inspector's own editor, passed in).
using ParticleCurveFn = std::function<bool(const char* id, okay::ParticleCurve&)>;
/// The Inspector rows added by the upgrade, and the live preview. Call it inside the Particle System's inspector.
bool DrawParticleInspectorExtras(okay::editor::EditorState& ed, okay::ParticleSystem& ps, const ParticleCurveFn& curveEditor);

} // namespace oked
