#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/Buildable.hpp"
#include "okay/Components/LogicLinks.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace okay {

class Scene;
class MeshRenderer;
class UIButton;
class TextRenderer;
class UIPanel;
class Canvas;

/// One entry of the build palette: a prefab players can place.
struct BuildPaletteItem {
    std::string name;     ///< shown in the hotbar
    std::string prefab;   ///< .okayprefab file it places
    std::string icon;     ///< optional hotbar icon (PNG/JPG)
};

/// Build mode's input bindings. Keys are the engine's key chars (letters,
/// digits, ' '; 0 = unbound); mouse buttons 0 left / 1 right / 2 middle (-1 =
/// unbound); gamepad ids as in Input (0 A, 1 B, 2 X, 3 Y, 4 Back, 5 Start, 6 L,
/// 7 R; -1 = unbound). The default keys stay clear of the stock controllers'
/// (Q / E lean, Z prone, C crafting, F flashlight).
struct BuildBindings {
    char toggle    = 'b';   ///< enter / leave build mode
    int  place     = 0;     ///< mouse: place / drop / pick in link mode
    int  remove    = 1;     ///< mouse: delete / cancel a move / clear the link pick
    char removeKey = 'x';   ///< key that deletes too (like the remove mouse button)
    char rotate    = 'r';   ///< turn the piece (Shift turns the other way)
    char move      = 'g';   ///< pick up the aimed object to move it (press again to drop)
    char duplicate = 'v';   ///< copy the aimed object and carry the copy
    char link      = 'l';   ///< switch link mode on / off
    char undo      = 'u';
    char redo      = 'y';
    char nextItem  = 0;     ///< next palette slot
    char prevItem  = 0;     ///< previous palette slot
    bool numberKeys = true; ///< 1-9 pick a palette slot
    bool mouseWheel = true; ///< the wheel cycles palette slots
    int  padToggle = 4, padPlace = 7, padRemove = 6, padRotate = 2, padNext = 3, padUndo = 1;
    int  padMove = -1, padDuplicate = -1, padLink = -1, padRedo = -1, padPrev = -1;
};

/// Where the next piece would go: the result of aiming.
struct BuildPlacement {
    bool  hit = false;              ///< the aim ray found a surface
    bool  valid = false;            ///< it can be placed there
    Vec3  position;                 ///< the piece's root position
    Quat  rotation;                 ///< the piece's root rotation
    GameObject* surface = nullptr;  ///< what it would sit on
    std::string reason;             ///< why it's invalid ("Blocked", "Limit reached", ...)
};

/// In-game building for players: add it to the player (or its camera) and players
/// can press B to enter build mode, then place prefabs from a palette, move,
/// rotate, copy and delete what they built, undo / redo, and wire objects up with
/// logic links (LogicSource -> LogicTarget). A ghost of the piece follows the aim
/// (from the main camera, centre of the screen), snapped to the grid and flush
/// against the surface, green where it fits and red where it doesn't.
///
/// Only objects with an editable Buildable can be changed, and everything
/// placed in build mode gets one (`placed` = true), so the level itself is safe.
/// Creations (the placed objects and their links) save to and load from a file
/// in the player's save folder: SaveCreation / LoadCreation.
///
/// The built-in hotbar is ordinary scene UI (a Canvas with buttons and text) that
/// only shows in build mode; set `showHotbar` off to draw your own and read
/// SelectedSlot() / Active() / Tool() from a script. Scripts and visual scripts
/// get On Object Placed / On Object Removed events, and On Logic Signal from
/// targets. Not to be confused with BuilderMode, the primitive-shape modelling
/// tool (F2 in Play).
class BuildMode : public Behaviour {
public:
    /// Objects build mode creates for itself (ghost, wires, hotbar) carry this tag.
    /// They're never saved with a scene or a creation.
    static constexpr const char* kRuntimeTag = "BuildModeRuntime";
    enum class Tool { Place = 0, Move = 1, Link = 2 };

    // ---- Settings ----
    std::vector<BuildPaletteItem> palette;
    float gridSize = 1.0f;                     ///< snap step (0 = no grid)
    float rotationStep = 90.0f;                ///< degrees per rotate press
    float maxReach = 8.0f;                     ///< how far from the player you can build
    std::uint32_t buildLayers = 0xFFFFFFFFu;   ///< collider layers you may build on (bit n = layer n)
    int   maxObjects = 250;                    ///< cap on placed objects (0 = no cap)
    bool  allowAirPlacement = false;           ///< place in mid-air at full reach when aiming at nothing
    bool  startActive = false;                 ///< start in build mode
    bool  showHotbar = true;                   ///< draw the built-in palette hotbar
    bool  showGhost = true;                    ///< draw the placement preview
    std::string creationFolder = "creations";  ///< save-folder subfolder creations go in
    Color validColor     = Color::FromBytes(80, 255, 120, 110);
    Color invalidColor   = Color::FromBytes(255, 80, 80, 110);
    Color highlightColor = Color::FromBytes(255, 214, 60);
    Color wireColor      = Color::FromBytes(90, 200, 255);
    Color wireOnColor    = Color::FromBytes(255, 230, 60);
    BuildBindings bindings;

    /// C++ hooks: called after a player places / removes an object (undo and redo
    /// included). Scripts get the same as on_object_placed / on_object_removed.
    std::function<void(GameObject*)> onPlaced, onRemoved;

    // ---- State ----
    bool Active() const { return m_active; }
    void SetActive(bool on);
    Tool CurrentTool() const;
    bool LinkMode() const { return m_linkMode; }
    void SetLinkMode(bool on);
    int  SelectedSlot() const { return m_slot; }
    void SelectSlot(int slot);
    float Yaw() const { return m_yaw; }
    void SetYaw(float degrees) { m_yaw = degrees; }
    /// The object being carried (move / copy), or nullptr.
    GameObject* Carried() const;
    /// The source picked in link mode, or nullptr.
    GameObject* LinkPick() const;
    /// The last aim result (updated every frame in build mode).
    const BuildPlacement& LastPlacement() const { return m_last; }

    // ---- Palette ----
    void ClearPalette();
    int  AddToPalette(const std::string& name, const std::string& prefab, const std::string& icon = "");

    // ---- Ray-driven actions (camera independent, so they can be tested) ----
    /// Where the selected palette piece would go along a ray.
    BuildPlacement ComputePlacement(Scene& scene, const Vec3& origin, const Vec3& dir, float reach = -1.0f);
    /// Place the selected palette piece along a ray. nullptr if it doesn't fit.
    GameObject* PlaceAt(Scene& scene, const Vec3& origin, const Vec3& dir, float reach = -1.0f);
    /// The editable object under a ray, or nullptr.
    GameObject* PickAt(Scene& scene, const Vec3& origin, const Vec3& dir, float reach = -1.0f);
    /// Delete the editable object under a ray. False if there's nothing to delete.
    bool RemoveAt(Scene& scene, const Vec3& origin, const Vec3& dir, float reach = -1.0f);
    /// Link mode click: the first click picks a source, the next a target, which
    /// links or unlinks it. Clicking empty space clears the pick.
    bool LinkClickAt(Scene& scene, const Vec3& origin, const Vec3& dir, float reach = -1.0f);

    // ---- Direct actions (all undoable) ----
    /// Place palette slot `slot` at a pose (respects maxObjects).
    GameObject* PlaceItem(int slot, const Vec3& position, const Quat& rotation);
    /// Can players edit `go` (it has an editable, live Buildable)?
    bool CanEdit(GameObject* go) const;
    /// Delete an editable object.
    bool Remove(GameObject* go);
    /// Move / turn an editable object to a pose.
    bool SetPose(GameObject* go, const Vec3& position, const Quat& rotation);
    /// Pick up an editable object; it follows the aim until DropCarried / CancelCarry.
    bool BeginMove(GameObject* go);
    /// Copy an editable object and carry the copy.
    GameObject* Duplicate(GameObject* go);
    /// Move the carried object to where a ray aims (camera independent).
    void UpdateCarry(Scene& scene, const Vec3& origin, const Vec3& dir, float reach = -1.0f);
    /// Put the carried object down where it is. False if it doesn't fit there.
    bool DropCarried();
    /// Put the carried object back (a copy is discarded).
    void CancelCarry();
    /// Link `source` (with a LogicSource) to `target` (with a LogicTarget).
    bool Link(GameObject* source, GameObject* target);
    bool Unlink(GameObject* source, GameObject* target);
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }
    void ClearHistory() { m_undo.clear(); m_redo.clear(); }
    /// Placed objects in the scene (what maxObjects caps).
    int PlacedCount() const;

    // ---- Creations ----
    /// Save every placed object and its links to `name` (a file in creationFolder
    /// inside the save folder, ".okaycreation" added) or to a path.
    bool SaveCreation(const std::string& name, std::string* error = nullptr);
    /// Replace what players placed with a saved creation (clears undo history).
    bool LoadCreation(const std::string& name, std::string* error = nullptr);
    /// Remove everything players placed (clears undo history).
    void ClearCreation();
    /// The file `name` saves to: `folder`/name.okaycreation resolved into the save
    /// folder (a name with a folder or extension is used as given).
    static std::string CreationPath(const std::string& name, const std::string& folder = "creations");

    // ---- Snapping (exposed for tests and custom tools) ----
    /// Root position that puts a piece with bounds [mn, mx] (relative to its root)
    /// flush on the surface at `point` (facing `normal`), with the two other axes
    /// snapped so the piece's edges land on the grid (lines through `anchor`).
    static Vec3 SnapToSurface(const Vec3& point, const Vec3& normal, const Vec3& mn, const Vec3& mx,
                              float grid, const Vec3& anchor = Vec3{0, 0, 0});
    /// World-space bounds of `root` and its children, relative to its position.
    static bool LocalBounds(const GameObject* root, Vec3& mn, Vec3& mx);

    void Update(float dt) override;
    void OnDestroy() override;
    void OnDisable() override;
    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override;

    BuildMode();
    ~BuildMode() override;

private:
    struct Template;
    struct Command {
        enum class Type { Place, Remove, Pose, Link, Unlink } type = Type::Place;
        int  id = 0, other = 0;
        std::string data;            // serialized object (place / remove)
        Vec3 p0, p1; Quat r0, r1;    // pose before / after
    };

    bool  m_active = false, m_started = false, m_linkMode = false;
    int   m_slot = 0;
    float m_yaw = 0.0f;
    int   m_nextId = 0;
    BuildPlacement m_last;
    std::vector<Command> m_undo, m_redo;
    std::map<std::string, std::unique_ptr<Template>> m_templates;

    // Carry (move / copy).
    std::uint64_t m_carry = 0;
    bool  m_carryNew = false, m_carryValid = false;
    Vec3  m_carryPos0, m_carryMn, m_carryMx;
    Quat  m_carryRot0;
    float m_carryYaw = 0.0f;

    // Link mode.
    std::uint64_t m_linkPick = 0;

    // Runtime visuals (all tagged kRuntimeTag).
    GameObject* m_ghost = nullptr;
    std::string m_ghostPrefab;
    bool m_ghostValid = true;
    GameObject* m_wireRoot = nullptr;
    std::vector<GameObject*> m_wires;
    std::uint64_t m_hl = 0;                        // highlighted object
    struct SavedOutline { MeshRenderer* mr; bool on; Color color; float width; Color emissive; };
    std::vector<SavedOutline> m_hlSaved;
    Color m_hlColor;
    GameObject* m_hud = nullptr;
    UIPanel* m_hudPanel = nullptr;
    std::vector<UIButton*> m_hudSlots;
    TextRenderer* m_hudTitle = nullptr;
    TextRenderer* m_hudHint = nullptr;
    std::string m_hudSig;

    Template* TemplateFor(const std::string& prefab);
    Template* SelectedTemplate();
    GameObject* Owner() const;
    bool AimRay(Vec3& origin, Vec3& dir, float& reach) const;
    float Reach(float r) const { return r >= 0.0f ? r : maxReach; }
    RaycastHit3D AimHit(Scene& s, const Vec3& origin, const Vec3& dir, float reach, GameObject* ignore) const;
    BuildPlacement Resolve(Scene& s, const Vec3& origin, const Vec3& dir, float reach,
                           const Vec3& mn, const Vec3& mx, const Quat& yawRot, const Quat& finalRot,
                           GameObject* ignore, bool counts);
    bool Blocked(Scene& s, const Vec3& pos, const Vec3& mn, const Vec3& mx, GameObject* ignore) const;
    int  NextId();
    Buildable* EnsureBuildable(GameObject* go);
    GameObject* FindId(int id) const;
    std::string Snapshot(GameObject* go);
    GameObject* Restore(const std::string& data);
    void Destroy(GameObject* go, bool notify);
    void Push(Command c);
    void Notify(GameObject* go, bool placed);
    bool ToggleLinkIds(int src, int dst, bool on);

    void SetHighlight(GameObject* go, const Color& c);
    void ClearHighlight();
    void UpdateGhost(Scene& s, const BuildPlacement& p);
    void HideGhost();
    void UpdateWires(Scene& s, bool show, const Vec3* aimPoint);
    void UpdateHud(Scene& s);
    bool HotbarHovered() const;
    void DestroyRuntime();
};

/// Saving and loading creations without a BuildMode (scripts, menus, servers).
/// A creation file holds every placed object (Buildable.placed), each written
/// with the scene format (so links, logic settings and any other component come
/// along), with doors and movers saved in their closed pose:
///     okaycreation 1
///     objects 2
///     object
///     okayscene 1
///     gameobject 0 "Button" ...
///     end
///     endobject
///     ...
/// Readers skip lines they don't know, and refuse files from a newer version.
class BuildCreation {
public:
    static constexpr int kVersion = 1;
    static std::string Serialize(const Scene& scene);
    static bool Save(const Scene& scene, const std::string& path, std::string* error = nullptr);
    /// Add a creation's objects (after removing what players placed, when `replace`).
    /// Ids that clash with objects already in the scene are renumbered, links included.
    static bool Deserialize(Scene& scene, const std::string& text, bool replace = true,
                            std::string* error = nullptr, std::vector<GameObject*>* added = nullptr);
    static bool Load(Scene& scene, const std::string& path, bool replace = true,
                     std::string* error = nullptr, std::vector<GameObject*>* added = nullptr);
    /// Remove every placed object.
    static void Clear(Scene& scene);
    /// One object as scene text, with doors / movers in their closed pose.
    static std::string SerializeObject(GameObject& root);
    /// The roots of every placed object, in scene order.
    static std::vector<GameObject*> PlacedRoots(const Scene& scene);
};

} // namespace okay
