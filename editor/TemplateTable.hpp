#pragma once
// The one table that turns a template's TITLE into the function that builds its scene.
//
// The launcher's Templates page, the launcher's New Project wizard and the editor's New Project form all list
// templates by title (shared/ProjectCreate.hpp: okayproj::Templates()). The launcher does not link the engine,
// so it can only pass the title on (--template "<Title>"); this table is where the editor finds the builder
// for it. Header-only and free of ImGui and SDL, so the editor and the tests (tests/test_templates_work.cpp)
// use exactly the same table.
//
// Adding a template: see docs/templates_check.md. The short version: one row in okayproj::Templates(), one row
// here, one builder in EditorState, one row in tests/test_templates_work.cpp. The test fails until all four agree.
//
//   FindBuilder(title)       the builder for exactly this title, or null when there is none
//   BuilderOrDefault(title)  what the editor runs for a title it was handed: the builder, or the 3D Scene when the
//                            title is empty (no --template was given) or unknown (`known` says which)
//   TemplateTable()          every row, for the tests
#include "EditorState.hpp"
#include <string>
#include <vector>

namespace oked {

/// A function that fills the editor's scene with one starter scene (EditorState::NewScene3D and friends).
using TemplateBuilder = void (okay::editor::EditorState::*)();

struct TemplateEntry {
    const char* title;       ///< exactly the title in okayproj::Templates()
    TemplateBuilder build;
};

/// Every template the editor can build, in the order the launcher lists them.
inline const std::vector<TemplateEntry>& TemplateTable() {
    using E = okay::editor::EditorState;
    static const std::vector<TemplateEntry> t = {
        {"3D Scene",                   &E::NewScene3D},
        {"2D Scene",                   &E::NewScene2D},
        {"Empty",                      &E::NewScene},
        {"First and Third Person",       &E::NewThirdPerson},
        {"Third Person Shooter",       &E::NewThirdPersonShooter},
        {"Platformer",                 &E::NewPlatformer},
        {"Top-Down",                   &E::NewTopDown},
        {"Top-Down Shooter",           &E::NewTopDownShooter},
        {"2.5D Platformer",            &E::NewPlatformer25D},
        {"Point & Click",              &E::NewPointAndClick},
        {"Vehicle (3D)",               &E::NewVehicle3D},
        {"Vehicle (get in and drive)", &E::NewVehicleSeats3D},
        {"Vehicle (2D)",               &E::NewVehicle2D},
        {"Sandbox",                    &E::NewVoxelSandbox},
        {"Multiplayer",                &E::NewMultiplayer},
        {"Main Menu",                  &E::NewMainMenu},
    };
    return t;
}

/// The builder for exactly `title`, or null when the table has none (a title the launcher lists that the editor does
/// not know would otherwise silently become a 3D Scene).
inline TemplateBuilder FindBuilder(const std::string& title) {
    // The two titles this one replaced (2.880 and older launchers, scripts and saved --template arguments still pass them).
    // They are not in the table: the table is what the New Project list shows.
    if (title == "First Person") return &okay::editor::EditorState::NewThirdPerson;   // starts behind the player; V goes to the eyes
    if (title == "Third Person") return &okay::editor::EditorState::NewThirdPerson;
    for (const TemplateEntry& e : TemplateTable())
        if (title == e.title) return e.build;
    return nullptr;
}

/// The builder the editor runs for a title it was handed. An empty title means no template was asked for and gives the
/// 3D Scene quietly; a title that is not in the table also gives the 3D Scene, and sets `*known` to false so the caller
/// can say so. `known` may be null.
inline TemplateBuilder BuilderOrDefault(const std::string& title, bool* known = nullptr) {
    if (known) *known = true;
    if (title.empty()) return &okay::editor::EditorState::NewScene3D;
    if (TemplateBuilder b = FindBuilder(title)) return b;
    if (known) *known = false;
    return &okay::editor::EditorState::NewScene3D;
}

}  // namespace oked
