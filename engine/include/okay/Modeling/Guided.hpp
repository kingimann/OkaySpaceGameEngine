#pragma once
// Guided procedural modeling: templates ("Fence") that generate an ordinary editable PolyMesh from a small recipe,
// and a regeneration step that never silently destroys what the user did by hand afterwards.
// See docs/guided_modeling_design.md. Written from general parametric modeling (points along a path, boxes and
// extruded profiles); it uses no code from any other project.
//
//   Recipe   what was asked for: template, parameters, a design per slot, colours, an optional ground path,
//            detached part ids and embedded user parts. Text form: "okayguided 1".
//   Generate pure and deterministic: recipe -> named parts (each part is a PolyMesh with a stable id like post/3).
//   Analyze  compares the CURRENT mesh with Generate(stored recipe): which parts are intact, edited or deleted,
//            and which faces are the user's own (custom).
//   Regenerate  applies a changed recipe: custom geometry is copied through untouched, intact parts are rebuilt,
//            edited parts need an explicit decision (keep / discard); nothing is overwritten silently.
//
// No <mutex> / <thread>: this header is reachable from the Windows editor.
#include "okay/Modeling/PolyMesh.hpp"
#include <array>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace okay::modeling::guided {

// ---- Definitions a template publishes (they drive the editor's panel) -------------------------------------

struct ParamDef {
    enum class Kind { Float, Int, Bool, Choice };
    std::string id, label, tip, group;
    Kind kind = Kind::Float;
    double def = 0, min = 0, max = 1;
    std::vector<std::string> choices;   ///< Choice: the value is the index
};

struct DesignDef { std::string id, name; };
struct SlotDef {
    std::string id, name;
    std::vector<DesignDef> designs;
    int defaultMaterial = 0;
    Color defaultColor{0.8f, 0.8f, 0.8f, 1.0f};
};

struct Recipe;
/// A ready-made starting point: the template with some parameters and designs already chosen.
struct StartDesign {
    std::string id, name, description;
    std::map<std::string, double> params;
    std::map<std::string, std::string> slotDesign;
    std::map<std::string, Color> colors;
};

/// A box a design must fill: origin at the bottom centre, three unit axes, and the size along each.
struct Frame {
    Vec3 origin{0, 0, 0};
    Vec3 t{1, 0, 0}, u{0, 1, 0}, n{0, 0, 1};
    float sx = 1, sy = 1, sz = 1;   ///< size along t, u, n
    Vec3 Point(float a, float b, float c) const { return origin + t * a + u * b + n * c; }   ///< a: -sx/2..sx/2, b: 0..sy, c: -sz/2..sz/2
};

struct GeneratedPart {
    std::string id;      ///< stable: slot + index, for example "post/3" or "infill/1/7"
    std::string slot;
    PolyMesh mesh;
};

// ---- User parts (a reusable piece made from a selection) --------------------------------------------------

/// Normalised to x,z in -0.5..0.5 and y in 0..1; a design fits it to the slot's frame.
struct UserPart {
    std::string id, name, slot;
    PolyMesh mesh;
};
std::string SerializeUserPart(const UserPart& p);
bool ParseUserPart(const std::string& text, UserPart& out, std::string* error = nullptr);
/// Normalise the chosen faces of `mesh` into a UserPart; false if none are usable.
bool MakeUserPart(const PolyMesh& mesh, const std::vector<int>& faces, const std::string& name, const std::string& slot,
                  UserPart& out, std::string* error = nullptr);
/// Fit a user part into a frame.
PolyMesh FitUserPart(const UserPart& p, const Frame& f);

// ---- Recipe ------------------------------------------------------------------------------------------------

struct Recipe {
    std::string templateId;
    int templateVersion = 1;
    std::map<std::string, double> params;
    std::map<std::string, std::string> slotDesign;
    std::map<std::string, std::string> partDesign;   ///< part id -> design (overrides the slot's)
    std::map<std::string, Color> colors;             ///< per slot
    std::map<std::string, int> materials;            ///< per slot (stored; drawn once per-face materials land)
    std::vector<Vec3> path;                          ///< ground path; empty = a straight run
    std::set<std::string> detached;                  ///< part ids that no longer follow the recipe
    std::map<std::string, UserPart> userParts;       ///< embedded, keyed by UserPart::id

    double Param(const std::string& id, double fallback = 0) const {
        auto it = params.find(id);
        return it == params.end() ? fallback : it->second;
    }
    std::string DesignFor(const std::string& slot, const std::string& partId) const;
    std::string Serialize() const;
    static bool Parse(const std::string& text, Recipe& out, std::string* error = nullptr);
};

// ---- Templates and the registry ---------------------------------------------------------------------------

struct Template {
    std::string id, name, description, category;   ///< category: Architecture, Vehicles, Characters, Environment, Props
    int version = 1;
    bool usesPath = false;   ///< follows Recipe::path when it has two or more points (fence, wall)
    std::vector<ParamDef> params;
    std::vector<SlotDef> slots;
    std::vector<StartDesign> starts;
    /// Build every part for a recipe whose params are already filled in and clamped.
    std::function<void(const Recipe&, std::vector<GeneratedPart>&)> generate;
    /// Optional compatibility rule applied after a random change (for example: no wing spoiler on a pickup).
    std::function<void(Recipe&)> constrain;
    /// Slots whose parts are not subdivided again at Ultra (their outline is already smoothed by the template, and Catmull-Clark would
    /// shrink and round away sharp features such as wheel arches).
    std::vector<std::string> noSubdivisionSlots;
};

class Registry {
public:
    static Registry& Instance();
    void Register(const Template& t);            ///< replaces a template with the same id
    const Template* Find(const std::string& id) const;
    std::vector<const Template*> InCategory(const std::string& category) const;
    const std::vector<Template>& All() const { return templates_; }
    static const std::vector<std::string>& Categories();
private:
    std::vector<Template> templates_;
};
/// Registers the built-in templates (the fence). Safe to call more than once.
void RegisterBuiltinTemplates();

/// A recipe with every parameter at its default and the first start design applied (or `startId`).
Recipe DefaultRecipe(const Template& t, const std::string& startId = "");
/// Clamp every parameter to its range; missing ones get the default. Returns a copy.
Recipe Normalize(const Template& t, const Recipe& r);

/// Build the parts. Parts whose id is in `recipe.detached` are skipped. Unknown template: empty and `error` set.
std::vector<GeneratedPart> Generate(const Recipe& r, std::string* error = nullptr);
/// Every part joined into one PolyMesh (what Create gives the object).
PolyMesh Assemble(const std::vector<GeneratedPart>& parts);
/// Generate + Assemble.
/// What the last builds cost: Create() calls answered from the cache (hits) or built (misses), parts reused by instancing,
/// and the milliseconds of the last real build. Counters only grow; the panel shows them.
struct GenStats { long hits = 0, misses = 0, instanced = 0; double lastBuildMs = 0; };
const GenStats& Stats();
/// Forget every cached build (tests; after registering a changed template).
void ClearCache();
PolyMesh Create(const Recipe& r, std::string* error = nullptr);

// ---- Validation and variation ----------------------------------------------------------------------------------

/// What is wrong, if anything, with a generated model.
struct ValidationReport {
    int parts = 0;
    int components = 0;        ///< groups of parts that touch or overlap (1 = one connected object)
    int nonFinite = 0, degenerate = 0, openParts = 0, invertedParts = 0, badIndices = 0;
    std::vector<std::string> problems;
    bool Ok() const { return nonFinite == 0 && degenerate == 0 && openParts == 0 && badIndices == 0; }
};
/// Checks every part: finite positions, no degenerate faces, closed (watertight), outward faces (positive volume), valid indices;
/// and counts the connected groups of parts (bounding boxes that touch). `checkVolume` is off for subdivided (Ultra) models.
ValidationReport Validate(const std::vector<GeneratedPart>& parts, bool checkVolume = true);

/// A repeatable variation of `base` (the start design's recipe): every setting moves within +-`strength` of its range (default 15 percent),
/// some parts get another design, then the template's compatibility rule runs. Same seed, same result.
Recipe Randomise(const Template& t, const Recipe& base, uint32_t seed, float strength = 0.15f);

// ---- Skeleton for animation (humans only) ------------------------------------------------------------------------------
/// One bone of the humanoid skeleton made for a human recipe, in the model's own space (bind pose). `tail` is where the bone ends
/// (the next joint, or past the hand, foot or head); the bones are in the engine's 15-bone humanoid order and use plain names
/// (pelvis, spine_01, head, upperarm_l, lowerarm_l, hand_l, ..., thigh_l, calf_l, foot_l, ...) that the humanoid retarget maps.
/// The model faces +Z; "_l" is on +X.
struct RigJoint { std::string name; int parent = -1; Vec3 pos, tail; };
/// The skeleton that fits this human's proportions. False for templates without one (only "human" has it).
bool HumanoidSkeleton(const Recipe& r, std::vector<RigJoint>& out);
/// Skin weights by distance to the bones: every vertex is bound to its three nearest bones (4 slots, sum 1).
void SkinVertices(const std::vector<Vec3>& verts, const std::vector<RigJoint>& joints,
                  std::vector<std::array<int, 4>>& idx, std::vector<std::array<float, 4>>& wt);

// ---- Analysis and regeneration ----------------------------------------------------------------------------

enum class PartState { Intact, Edited, Deleted };
struct PartInfo {
    std::string id, slot;
    PartState state = PartState::Intact;
    std::string why;   ///< plain words for Edited / Deleted
};
struct Analysis {
    bool ok = false;
    std::string error;
    std::vector<PartInfo> parts;
    std::vector<std::string> faceOwner;   ///< per face of the analysed mesh: part id, or "" for custom
    int customFaces = 0;
    int Count(PartState s) const { int n = 0; for (const auto& p : parts) if (p.state == s) ++n; return n; }
};
Analysis Analyze(const PolyMesh& current, const Recipe& stored);

enum class Decision { Ask, Keep, Discard };
struct RegenOptions {
    Decision all = Decision::Ask;                      ///< for every edited part not listed below
    std::map<std::string, Decision> perPart;
};
struct RegenResult {
    bool ok = false;
    bool needsDecision = false;                        ///< edited parts exist and no decision covers them; nothing changed
    std::string message;
    std::vector<PartInfo> conflicts;                   ///< the edited / deleted parts that need a decision
    PolyMesh mesh;
    Recipe recipe;                                     ///< the recipe to store with the new mesh
    int regenerated = 0, kept = 0, discarded = 0, customFaces = 0;
};
/// Change a model from `stored` to `next`. `current` is the mesh as it is now (with the user's edits).
RegenResult Regenerate(const PolyMesh& current, const Recipe& stored, const Recipe& next, const RegenOptions& opt = {});
/// One part stops following the recipe; the mesh is untouched. False if the id is not a part of the recipe.
bool DetachPart(Recipe& r, const std::string& partId);

// ---- Files ------------------------------------------------------------------------------------------------

/// The sidecar next to a model asset: "<dir>/Fence.okaymesh" -> "<dir>/Fence.okayguided".
std::string SidecarPath(const std::string& meshPath);
bool SaveRecipe(const std::string& path, const Recipe& r, std::string* error = nullptr);
bool LoadRecipe(const std::string& path, Recipe& r, std::string* error = nullptr);
/// "Convert to regular mesh": the mesh file is not touched; only the recipe (sidecar) is deleted, so the model is an
/// ordinary mesh from now on. True if there was a sidecar and it is gone.
bool ConvertToRegular(const std::string& meshPath, std::string* error = nullptr);

/// A named recipe saved for reuse (`.okaytemplate`).
struct UserTemplate { std::string name, description; Recipe recipe; };
std::string SerializeUserTemplate(const UserTemplate& t);
bool ParseUserTemplate(const std::string& text, UserTemplate& out, std::string* error = nullptr);
/// Write / read a file in a folder, and list a folder's templates / parts (sorted by file name, bad files skipped).
bool SaveUserTemplate(const std::string& path, const UserTemplate& t, std::string* error = nullptr);
bool SaveUserPart(const std::string& path, const UserPart& p, std::string* error = nullptr);
std::vector<UserTemplate> LoadUserTemplates(const std::string& dir);
std::vector<UserPart> LoadUserParts(const std::string& dir);

// ---- Undo for guided steps ---------------------------------------------------------------------------------

struct GuidedState { PolyMesh mesh; Recipe recipe; bool guided = true; };
class GuidedHistory {
public:
    void Push(const GuidedState& before);          ///< call with the state BEFORE a step; clears redo
    bool Undo(GuidedState& current);               ///< swaps `current` with the last pushed state
    bool Redo(GuidedState& current);
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    void Clear() { undo_.clear(); redo_.clear(); }
private:
    std::vector<GuidedState> undo_, redo_;
};

/// Number of triangles a mesh turns into (a face with n corners makes n - 2).
int TriangleCount(const PolyMesh& m);
/// The core "Detail" setting every template gets: Low, Medium, High, Ultra (Medium when a recipe has none).
extern const char* const kDetailParam;
inline float DetailOf(const Recipe& r) { return (float)r.Param("detail", 1); }

// ---- Shape helpers (also used by templates in other files) --------------------------------------------------

namespace shapes {
/// The detail the parts being generated are built at, 0 to 3 continuous: 0 Low (blocky, few segments), 1 Medium (rounded),
/// 2 High (smooth, fine detail parts), 3 Ultra (subdivided, finest detail). Detail() is the nearest whole level.
/// Set for the duration of Generate() from the recipe's "detail" setting; the shape helpers below follow it
/// (fewer or more segments on curves, bevelled edges from High up). Templates can read it for extra detail parts.
int Detail();
float DetailF();
/// `base` segments at Medium: half at Low, double at High, four times at Ultra (never below `minimum`).
int Scale(int base, int minimum = 3);
/// A box filling the frame: 6 quads, outward.
PolyMesh Box(const Frame& f);
/// A prism with `sides` corners along the frame's up axis, fitted to the frame (cx, cz radius = size/2).
PolyMesh Prism(const Frame& f, int sides);
/// A cone / pyramid with `sides` corners and a flat base, apex at the top of the frame.
PolyMesh Pyramid(const Frame& f, int sides);
/// A low-poly ball filling the frame.
PolyMesh Ball(const Frame& f, int rings, int sectors);
/// A flat profile (counter-clockwise corners in a,b = along t and up, within the frame's box) pushed through the frame's n size.
PolyMesh ExtrudeProfile(const Frame& f, const std::vector<Vec2>& profile);
void Append(PolyMesh& dst, const PolyMesh& src);
/// A closed solid: `profile` (counter-clockwise in the a,b plane, positions o + a*pa + b*pb) pushed along `nrm`
/// from -depth/2 to +depth/2. Faces are wound outward whatever the handedness of (a, b, nrm).
PolyMesh ExtrudeAxes(const Vec3& o, const Vec3& a, const Vec3& b, const Vec3& nrm, const std::vector<Vec2>& profile, float depth);
/// Flip faces whose normal points toward `centre` (right for convex shapes only).
void OrientOutward(PolyMesh& m, const Vec3& centre);
void Paint(PolyMesh& m, int material, const Color& color);
}

} // namespace okay::modeling::guided
