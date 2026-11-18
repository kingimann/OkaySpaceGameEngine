#pragma once
// The Scene view's right-click "add here" menu as plain data (no ImGui, no scene): is a right-button press a click or the start of a
// drag, where the new object goes (a surface under the cursor, else the ground, else a fixed distance in front of the camera; the
// clicked point in 2D; snapped when snapping is on), the menu entries and the search box. The host (editor/main.cpp, SceneAddMenuTick)
// fills a MenuState, draws the entries with MenuEntriesUI.hpp and runs the ids. The create.* entries are the Hierarchy create slots
// (DrawCreateSlot in main.cpp): the same creator functions as GameObject > Create and the Hierarchy [+] button, not a second copy.
// tests/test_scene_add_menu.cpp checks all of this without a window.
//
// Written from the owner's request (2026-11-18) and the existing Hierarchy menu code; ray-against-a-plane is the plain
// t = (planeY - originY) / dirY formula. No code from another project.
#include "MenuBarModel.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace oked {
namespace sceneadd {

using menubar::Entry;
using menubar::Kind;

// ---- 1. click or drag --------------------------------------------------------------------------------------------------------

/// A right-button press and release is a click (opens the menu) when the pointer moved at most `maxMovePx` pixels and the button was
/// down at most `maxSeconds`. Anything more is a drag (orbit / look / pan) or fly mode (button held while WASD moves) and stays as it was.
struct ClickLimits { float maxMovePx = 4.0f; float maxSeconds = 0.35f; };

inline bool IsClick(float dx, float dy, float seconds, ClickLimits lim = ClickLimits{}) {
    if (!(seconds >= 0.0f) || seconds > lim.maxSeconds) return false;
    return dx * dx + dy * dy <= lim.maxMovePx * lim.maxMovePx + 1e-4f;
}

// ---- 2. where the new object goes --------------------------------------------------------------------------------------------

struct P3 { float x = 0, y = 0, z = 0; };
struct Ray { P3 origin; P3 dir; };

enum class PlaceSource { Surface, Ground, InFront, World2D };
struct Placement { P3 pos; PlaceSource source = PlaceSource::InFront; };

inline constexpr float kDefaultFallbackDistance = 10.0f;   // metres in front of the camera when nothing is hit
inline constexpr float kMaxGroundDistance = 500.0f;        // a ground hit further than this (grazing the horizon) is not used

/// 3D: a surface hit (the caller's ray against the scene) wins; else the horizontal ground plane at `groundY`, when the ray reaches it in
/// front of the camera within kMaxGroundDistance; else `fallbackDistance` along the ray.
inline Placement PlaceIn3D(const Ray& r, bool surfaceHit, const P3& surfacePoint, float fallbackDistance, float groundY = 0.0f) {
    Placement p;
    if (surfaceHit) { p.pos = surfacePoint; p.source = PlaceSource::Surface; return p; }
    const float len = std::sqrt(r.dir.x * r.dir.x + r.dir.y * r.dir.y + r.dir.z * r.dir.z);
    if (len < 1e-8f) { p.pos = r.origin; p.source = PlaceSource::InFront; return p; }
    const P3 d{r.dir.x / len, r.dir.y / len, r.dir.z / len};
    if (std::fabs(d.y) > 1e-4f) {
        const float t = (groundY - r.origin.y) / d.y;
        if (t > 0.0f && t <= kMaxGroundDistance) {
            p.pos = {r.origin.x + d.x * t, groundY, r.origin.z + d.z * t};
            p.source = PlaceSource::Ground;
            return p;
        }
    }
    p.pos = {r.origin.x + d.x * fallbackDistance, r.origin.y + d.y * fallbackDistance, r.origin.z + d.z * fallbackDistance};
    p.source = PlaceSource::InFront;
    return p;
}

/// 2D: the clicked world position on the z = 0 plane.
inline Placement PlaceIn2D(float worldX, float worldY) {
    Placement p; p.pos = {worldX, worldY, 0.0f}; p.source = PlaceSource::World2D; return p;
}

/// Ray against a box (slab test): true with the distance `t` along the (not necessarily unit) direction to where the ray ENTERS the box
/// [lo, hi]. A ray that starts inside the box, points away from it or misses gives false. Used in an object's own space (the host
/// carries the ray into it) so a rotated or scaled mesh is hit on its real faces, not on a bigger world box.
inline bool RayBoxEntry(const P3& o, const P3& d, const P3& lo, const P3& hi, float& t) {
    float tmin = -1e30f, tmax = 1e30f;
    const float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z}, l[3] = {lo.x, lo.y, lo.z}, h[3] = {hi.x, hi.y, hi.z};
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dd[i]) < 1e-12f) { if (oo[i] < l[i] || oo[i] > h[i]) return false; continue; }
        float t0 = (l[i] - oo[i]) / dd[i], t1 = (h[i] - oo[i]) / dd[i];
        if (t0 > t1) std::swap(t0, t1);
        tmin = std::max(tmin, t0); tmax = std::min(tmax, t1);
        if (tmin > tmax) return false;
    }
    if (tmin <= 0.0f) return false;   // starts inside or the box is behind the ray
    t = tmin;
    return true;
}

inline float SnapValue(float v, float step, bool on) {
    if (!on || !(step > 1e-6f)) return v;
    return std::round(v / step) * step;
}

/// Snap to the editor's increment. A surface or ground hit keeps its height (the object must still sit on it): only x and z snap there.
/// In the air (in front of the camera) all three snap; in 2D x and y snap and z stays 0.
inline Placement SnapPlacement(Placement p, float step, bool on) {
    if (!on) return p;
    p.pos.x = SnapValue(p.pos.x, step, true);
    if (p.source == PlaceSource::World2D) p.pos.y = SnapValue(p.pos.y, step, true);
    else if (p.source == PlaceSource::InFront) { p.pos.y = SnapValue(p.pos.y, step, true); p.pos.z = SnapValue(p.pos.z, step, true); }
    else p.pos.z = SnapValue(p.pos.z, step, true);
    return p;
}

/// How far to lift an object so it sits ON the surface or the ground instead of being half buried: `localMinY` is the lowest point of
/// its bounds relative to its pivot (negative for a pivot in the middle). Never pushes it down; in the air and in 2D no lift.
inline float RestOffsetY(PlaceSource src, float localMinY) {
    if (src != PlaceSource::Surface && src != PlaceSource::Ground) return 0.0f;
    return localMinY < 0.0f ? -localMinY : 0.0f;
}

// ---- 3. the shapes (shared with the Hierarchy create menu, which loops over these same tables) ---------------------------------

/// `label` is what the menu shows, `mesh` the name EditorState::CreateMesh takes.
struct MeshEntry { const char* label; const char* mesh; };

inline constexpr MeshEntry kPrimitives[] = {
    {"Cube", "Cube"}, {"Sphere", "Sphere"}, {"Cylinder", "Cylinder"}, {"Capsule", "Capsule"}, {"Cone", "Cone"}, {"Plane", "Plane"},
    {"Pyramid", "Pyramid"}, {"Wedge", "Wedge"}, {"Torus", "Torus"}, {"Torus Knot", "TorusKnot"}, {"Icosphere", "Icosphere"},
    {"Hemisphere", "Hemisphere"}, {"Stairs", "Stairs"}, {"Gear", "Gear"}, {"Prism", "Prism"}, {"Octahedron", "Octahedron"},
    {"Disc", "Disc"}, {"Tetrahedron", "Tetrahedron"}, {"Bipyramid", "Bipyramid"}, {"Rounded Box", "RoundedBox"}, {"Quad", "Quad"},
};
inline constexpr MeshEntry kNature[] = {
    {"Tree", "Tree"}, {"Pine", "Pine"}, {"PalmTree", "PalmTree"}, {"DeadTree", "DeadTree"}, {"Bush", "Bush"}, {"Rock", "Rock"},
    {"Mushroom", "Mushroom"}, {"Cactus", "Cactus"}, {"Crystal", "Crystal"},
};
inline constexpr MeshEntry kProps[] = {
    {"Barrel", "Barrel"}, {"Crate", "Crate"}, {"Fence", "Fence"}, {"Well", "Well"}, {"StreetLamp", "StreetLamp"},
    {"Campfire", "Campfire"}, {"Lantern", "Lantern"}, {"Chest", "Chest"}, {"Signpost", "Signpost"}, {"Cart", "Cart"},
    {"Tent", "Tent"}, {"Bench", "Bench"},
};
inline constexpr MeshEntry kStructures[] = {
    {"House", "House"}, {"Tower", "Tower"}, {"Windmill", "Windmill"}, {"Bridge", "Bridge"}, {"RockArch", "RockArch"},
};

// ---- 4. the menu -------------------------------------------------------------------------------------------------------------

inline constexpr const char* kIdEmpty = "scene.empty";
inline constexpr const char* kIdPrefabs = "scene.prefabs";
inline constexpr const char* kIdPrefabPrefix = "scene.prefab:";
inline constexpr const char* kIdDuplicate = "scene.duplicate";
inline constexpr const char* kIdDelete = "scene.delete";
inline constexpr const char* kIdFocus = "scene.focus";
inline constexpr const char* kIdRename = "scene.rename";
inline constexpr const char* kIdCopy = "scene.copy";
inline constexpr const char* kIdPaste = "scene.paste";

inline constexpr int kMaxPrefabsInMenu = 30;    // a longer list is cut (the search box finds the rest)
inline constexpr int kMaxSearchResults = 24;

struct MenuState {
    bool is2D = false;                  // a 2D scene: the sprite group replaces the 3D shapes
    bool hasObjectUnder = false;        // something is under the cursor
    std::string underName;              // its name
    bool clipboardHasObject = false;
    std::vector<std::string> prefabs;   // project prefabs, paths relative to Assets, "/" separated
};

/// "Enemies/Goblin.okayprefab" -> "Goblin".
inline std::string PrefabLabel(const std::string& path) {
    std::string n = path;
    const std::size_t slash = n.find_last_of("/\\");
    if (slash != std::string::npos) n = n.substr(slash + 1);
    const std::string ext = ".okayprefab";
    if (n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0) n.resize(n.size() - ext.size());
    return n;
}

/// The prefab path an id carries, or "" when the id is not a prefab id.
inline std::string PrefabPathFromId(const std::string& id) {
    const std::string pre = kIdPrefabPrefix;
    return id.size() > pre.size() && id.compare(0, pre.size(), pre) == 0 ? id.substr(pre.size()) : std::string();
}

/// The right-click menu: the groups first (plain words), then what can be done to the object under the cursor.
inline std::vector<Entry> BuildMenu(const MenuState& s) {
    using menubar::Item; using menubar::Off; using menubar::OnlyIf; using menubar::Sep; using menubar::Sub; using menubar::SlotMenu;
    std::vector<Entry> m;
    if (s.is2D) m.push_back(SlotMenu("create.2d", "Sprites & Shapes", "Sprites, premade shapes, sprite-sheet characters and tilemaps."));
    else        m.push_back(SlotMenu("create.3d", "Shapes & Things", "Cube, sphere, plane, cylinder, trees, houses, terrain, water and more."));
    m.push_back(SlotMenu("create.light", "Lights", "Point, spot and directional lights."));
    m.push_back(SlotMenu("create.camera", "Camera", "A camera for the game."));
    m.push_back(Item(kIdEmpty, "Empty Object", "", "An object with nothing on it yet. Add things to it in the Inspector."));
    m.push_back(SlotMenu("create.player", "Character / Player", "A player you can walk around with, ready to play."));
    m.push_back(SlotMenu("create.ui", "UI (menus, text, buttons)", "Canvas, text, button, image and more."));
    m.push_back(SlotMenu("create.audio", "Audio", "A sound or a music player."));
    m.push_back(SlotMenu("create.effects", "Effects (particles)", "Particles, decals, cables, foliage and volumes."));
    {
        std::vector<Entry> kids;
        if (s.prefabs.empty()) {
            kids.push_back(Off(Item("scene.prefabs.none", "No prefabs in this project yet"),
                               "Right-click an object in the Hierarchy and pick Save As Prefab, then it shows up here."));
        } else {
            int n = 0;
            for (const std::string& p : s.prefabs) {
                if (n++ >= kMaxPrefabsInMenu) break;
                kids.push_back(Item(std::string(kIdPrefabPrefix) + p, PrefabLabel(p), "", p));
            }
            if ((int)s.prefabs.size() > kMaxPrefabsInMenu) {
                kids.push_back(Sep());
                kids.push_back(Off(Item("scene.prefabs.more", std::to_string((int)s.prefabs.size() - kMaxPrefabsInMenu) + " more: use the search box"),
                                   "Type its name in the search box at the top of this menu."));
            }
        }
        m.push_back(Sub(kIdPrefabs, "Prefabs", std::move(kids), "Objects you saved in this project. Click one to place a copy here."));
    }
    if (s.hasObjectUnder) {
        const std::string nm = s.underName.empty() ? std::string("Object") : s.underName;
        m.push_back(Sep());
        m.push_back(Item(kIdFocus, "Focus on " + nm, "", "Move the view to look at it."));
        m.push_back(Item(kIdRename, "Rename " + nm));
        m.push_back(Item(kIdDuplicate, "Duplicate " + nm));
        m.push_back(Item(kIdCopy, "Copy " + nm));
        m.push_back(OnlyIf(Item(kIdPaste, "Paste Here", "", "Paste the copied object where you right-clicked."), s.clipboardHasObject, menubar::kNothingCopied));
        m.push_back(Item(kIdDelete, "Delete " + nm, "", "Delete it. Ctrl+Z brings it back."));
    } else if (s.clipboardHasObject) {
        m.push_back(Sep());
        m.push_back(Item(kIdPaste, "Paste Here", "", "Paste the copied object where you right-clicked."));
    }
    return m;
}

// ---- 5. the search box -------------------------------------------------------------------------------------------------------

/// One thing the search box can add. `id` tells the host what to create: "mesh:<mesh name>", "light:point", "camera", "empty", ...
struct SearchItem {
    std::string id, label, group, keywords;
};

inline std::vector<SearchItem> Catalog(bool is2D) {
    std::vector<SearchItem> c;
    auto add = [&](const std::string& id, const std::string& label, const std::string& group, const std::string& kw = "") {
        c.push_back({id, label, group, kw});
    };
    if (is2D) {
        add("2d:sprite", "Sprite", "Sprites", "picture image 2d");
        add("2d:tilemap", "Tilemap", "Sprites", "tiles level map ground 2d");
    } else {
        for (const MeshEntry& e : kPrimitives) add(std::string("mesh:") + e.mesh, e.label, "Shapes");
        for (const MeshEntry& e : kNature) add(std::string("mesh:") + e.mesh, e.label, "Nature", "plant forest");
        for (const MeshEntry& e : kProps) add(std::string("mesh:") + e.mesh, e.label, "Props");
        for (const MeshEntry& e : kStructures) add(std::string("mesh:") + e.mesh, e.label, "Structures", "building");
        add("terrain", "Terrain", "Land and water", "ground hills grass landscape");
        add("water", "Water", "Land and water", "sea lake river ocean");
        add("character", "Character", "Characters", "person human people body");
    }
    add("light:directional", "Directional Light", "Lights", "sun lamp");
    add("light:point", "Point Light", "Lights", "lamp bulb");
    add("light:spot", "Spot Light", "Lights", "lamp torch flashlight");
    add("camera", "Camera", "Camera", "view");
    add("empty", "Empty Object", "Empty", "nothing group folder");
    if (!is2D) add("player:controller", "Player Controller", "Character / Player", "character hero walk first third person");
    add("audio:source", "Audio Source", "Audio", "sound noise effect");
    add("audio:music", "Music Player", "Audio", "song playlist tracks");
    add("particles", "Particle System", "Effects", "fire smoke sparks explosion magic");
    return c;
}

namespace detail {
inline std::string Lower(std::string s) {
    for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
    return s;
}
inline std::vector<std::string> Words(const std::string& s) {
    std::vector<std::string> w; std::string cur;
    for (char ch : s) {
        if (std::isspace((unsigned char)ch)) { if (!cur.empty()) { w.push_back(cur); cur.clear(); } }
        else cur.push_back((char)std::tolower((unsigned char)ch));
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
}
}  // namespace detail

/// What the typed text finds: every word must appear in the label, group or keywords (any order, any case). Best matches first: the exact
/// name, then names that start with the text, then names that contain it, then keyword matches. Empty text finds nothing.
/// Project prefabs are searched by file name. At most kMaxSearchResults.
inline std::vector<SearchItem> Search(const std::string& query, bool is2D, const std::vector<std::string>& prefabs) {
    const std::vector<std::string> words = detail::Words(query);
    if (words.empty()) return {};
    std::string whole;
    for (std::size_t i = 0; i < words.size(); ++i) { if (i) whole += ' '; whole += words[i]; }
    std::vector<SearchItem> all = Catalog(is2D);
    for (const std::string& p : prefabs) all.push_back({std::string(kIdPrefabPrefix) + p, PrefabLabel(p), "Prefabs", "prefab"});
    std::vector<std::pair<int, std::size_t>> hits;   // (score, catalog index)
    for (std::size_t i = 0; i < all.size(); ++i) {
        const std::string label = detail::Lower(all[i].label);
        const std::string hay = label + " " + detail::Lower(all[i].group) + " " + detail::Lower(all[i].keywords);
        bool ok = true;
        for (const std::string& w : words) if (hay.find(w) == std::string::npos) { ok = false; break; }
        if (!ok) continue;
        int score = 4;
        if (label == whole) score = 0;
        else if (label.compare(0, whole.size(), whole) == 0) score = 1;
        else if (label.find(whole) != std::string::npos) score = 3;
        else {
            bool allInLabel = true;
            for (const std::string& w : words) if (label.find(w) == std::string::npos) { allInLabel = false; break; }
            if (allInLabel) score = 2;
        }
        hits.push_back({score, i});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<SearchItem> out;
    for (const auto& h : hits) {
        if ((int)out.size() >= kMaxSearchResults) break;
        out.push_back(all[h.second]);
    }
    return out;
}

/// Up / Down in the result list: wraps round; a selection past the end (the list got shorter) is pulled back in.
inline int MoveSelection(int selected, int delta, int count) {
    if (count <= 0) return 0;
    if (delta == 0) return selected < 0 ? 0 : (selected >= count ? count - 1 : selected);
    int n = selected + delta;
    n %= count;
    if (n < 0) n += count;
    return n;
}

}  // namespace sceneadd
}  // namespace oked
