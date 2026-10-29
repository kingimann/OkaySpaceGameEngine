#pragma once
// Behavior trees for NPC AI: the tree asset (a `.okaybt` text file), its
// blackboard keys and the node catalogue the editor, the runner and the
// scripts share. The BehaviorTreeRunner component runs a tree on an NPC.
#include "okay/Math/Vec3.hpp"
#include "okay/VisualScript/VsValue.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace okay {

class Scene;
class GameObject;

/// What a node reported the last time it ran.
enum class BtStatus { Invalid = 0, Running, Success, Failure, Aborted };
const char* BtStatusName(BtStatus s);

/// A blackboard value: a number, a bool, a string, a vector or an object reference.
struct BtValue {
    enum class Type { Number = 0, Bool, String, Vector, Object };
    Type type = Type::Number;
    float number = 0.0f;
    bool  boolean = false;
    std::string text;              ///< String; for an Object, the object's name
    Vec3  vector{0, 0, 0};
    std::uint64_t objectId = 0;    ///< Object: the live instance (runtime only; 0 = look the name up)

    static BtValue Number(float v) { BtValue b; b.type = Type::Number; b.number = v; return b; }
    static BtValue Bool(bool v) { BtValue b; b.type = Type::Bool; b.boolean = v; return b; }
    static BtValue String(std::string s) { BtValue b; b.type = Type::String; b.text = std::move(s); return b; }
    static BtValue Vector(const Vec3& v) { BtValue b; b.type = Type::Vector; b.vector = v; return b; }
    static BtValue Object(std::string name, std::uint64_t id = 0) { BtValue b; b.type = Type::Object; b.text = std::move(name); b.objectId = id; return b; }

    float AsNumber() const;
    bool  AsBool() const;
    std::string AsString() const;
    Vec3  AsVector() const;
    /// The referenced object: the live instance while it exists, else the name looked up in `scene`.
    GameObject* AsObject(Scene* scene) const;
    /// An object value that names or holds something.
    bool IsSet() const { return type != Type::Object || objectId != 0 || !text.empty(); }
    bool operator==(const BtValue& o) const;

    /// Text in the type's own notation ("3.5", "true", "hello", "1 2 3", "Player").
    std::string ToText() const;
    /// The inverse of ToText for a value of `type`.
    static BtValue FromText(Type type, const std::string& text);
    /// A value from free text: a number when it reads as one, true / false, "x y z", else a string.
    static BtValue Infer(const std::string& text);
    /// Conversions for scripts and visual scripts.
    vs::VsValue ToVs() const;
    static BtValue FromVs(const vs::VsValue& v);
    /// Keep this entry's type while taking `v` (a number written into a bool key becomes a bool).
    BtValue Converted(const BtValue& v) const;

    static const char* TypeName(Type t);
    static bool TypeFromName(const std::string& n, Type& out);
};

/// Named, typed values an NPC's tree, its scripts and the editor read and write.
/// The tree declares the keys and their defaults; a runner may override them.
class Blackboard {
public:
    struct Entry { std::string name; BtValue value; };

    const std::vector<Entry>& Entries() const { return m_entries; }
    bool Has(const std::string& name) const { return Find(name) != nullptr; }
    const BtValue* Find(const std::string& name) const;
    BtValue* Find(const std::string& name);
    /// Set a value. An existing key keeps its declared type (the value is converted);
    /// a new key takes the value's type.
    void Set(const std::string& name, const BtValue& v);
    /// Declare (or redeclare) a key with its type and default.
    void Declare(const std::string& name, const BtValue& def);
    void Remove(const std::string& name);
    void Clear() { m_entries.clear(); ++m_version; }

    float GetNumber(const std::string& name, float fallback = 0.0f) const;
    bool  GetBool(const std::string& name, bool fallback = false) const;
    std::string GetString(const std::string& name, const std::string& fallback = std::string()) const;
    Vec3  GetVector(const std::string& name, const Vec3& fallback = Vec3{0, 0, 0}) const;
    GameObject* GetGameObject(const std::string& name, Scene* scene) const;   // not GetObject: a windows.h macro
    /// A position from a key: a vector, or an object's position (false when neither).
    bool GetPosition(const std::string& name, Scene* scene, Vec3& out) const;

    /// Bumps on every change (the editor's live view redraws on it).
    std::uint32_t Version() const { return m_version; }

private:
    std::vector<Entry> m_entries;
    std::uint32_t m_version = 0;
};

/// The node catalogue. Composites run children; decorators wrap one child;
/// tasks do things; services tick alongside a node while it runs.
enum class BtNodeType {
    // ---- composites ----
    Sequence = 0, Selector, Parallel, RandomSelector,
    // ---- decorators ----
    Inverter, Succeeder, Repeat, UntilFail, Cooldown, TimeLimit, Condition, Guard,
    // ---- tasks ----
    Wait, MoveTo, Patrol, Wander, Chase, Flee, LookAt, PlayAnimation, Attack,
    SetBlackboard, SendEvent, RunScript, PlayAudio, Custom,   // PlayAudio: windows.h defines PlaySound
    // ---- services ----
    Perception, HealthWatcher,
    Count
};
enum class BtNodeKind { Composite = 0, Decorator, Task, Service };

/// One parameter a node type takes (the editor builds its inspector from these).
struct BtParamDef {
    enum Kind {
        Number,     ///< a float
        Int,        ///< an integer
        Bool,       ///< 0 / 1
        Text,       ///< free text
        Vector,     ///< "x y z"
        Key,        ///< a blackboard key name (`choices` lists the key types it accepts, "number|bool|...", or null for any)
        Target,     ///< a blackboard key (object or vector) or a scene object's name
        Object,     ///< a scene object's name
        Choice,     ///< one of `choices` ("a|b|c")
        Waypoints,  ///< comma-separated scene object names
        Event,      ///< a script event name
        Function,   ///< a script function name
        File        ///< an asset path
    };
    const char* key;
    const char* label;
    Kind kind;
    const char* def;        ///< the default, as text
    const char* hint;       ///< tooltip
    const char* choices;    ///< Choice: "a|b|c"; Key: accepted types "number|bool|string|vector|object"
};

struct BtNodeDef {
    BtNodeType type;
    const char* name;     ///< the file / script name ("MoveTo")
    const char* title;    ///< shown in the editor ("Move To")
    BtNodeKind kind;
    const char* doc;      ///< one or two sentences for the palette and inspector
    std::vector<BtParamDef> params;
};
const std::vector<BtNodeDef>& BtNodeDefs();
const BtNodeDef& BtDefOf(BtNodeType t);
bool BtTypeFromName(const std::string& name, BtNodeType& out);
inline BtNodeKind BtKindOf(BtNodeType t) { return BtDefOf(t).kind; }
inline bool BtIsService(BtNodeType t) { return BtKindOf(t) == BtNodeKind::Service; }
/// The parameter a node of `type` takes under `key`, or null.
const BtParamDef* BtParamOf(BtNodeType type, const std::string& key);

struct BtNode {
    int id = 0;
    BtNodeType type = BtNodeType::Sequence;
    std::string name;                 ///< an optional label shown on the canvas
    int parent = -1;
    std::vector<int> children;        ///< flow children and services, in order
    float x = 0.0f, y = 0.0f;         ///< editor position (graph units)
    std::map<std::string, std::string> params;   ///< parameters as text (the catalogue gives the defaults)

    /// Typed access with the catalogue's default when the parameter is unset.
    float Num(const char* key) const;
    int   Int(const char* key) const;
    bool  Flag(const char* key) const;
    std::string Str(const char* key) const;
    Vec3  Vec(const char* key) const;
    bool  Has(const char* key) const { return params.count(key) != 0; }
    void  Set(const char* key, const std::string& v) { params[key] = v; }
    void  Set(const char* key, const char* v) { params[key] = v ? v : ""; }
    void  Set(const char* key, float v);
    void  Set(const char* key, bool v) { params[key] = v ? "1" : "0"; }
    void  Set(const char* key, int v) { params[key] = std::to_string(v); }
    void  Set(const char* key, const Vec3& v);
    BtNodeKind Kind() const { return BtKindOf(type); }
    const BtNodeDef& Def() const { return BtDefOf(type); }
};

/// A tree: its blackboard keys with defaults, the nodes and which one is the root.
/// Text asset `.okaybt`: "okaybt 1", `name`, `key` and `node` lines; children in
/// file order under their `parent=`.
class BehaviorTree {
public:
    std::string name;
    std::vector<Blackboard::Entry> keys;
    std::vector<BtNode> nodes;
    int root = -1;

    BtNode* Find(int id);
    const BtNode* Find(int id) const;
    int IndexOf(int id) const;
    /// Add a node under `parent` (-1: a free node; the first one becomes the root)
    /// at sibling position `at` (-1 = last). Returns its id.
    int AddNode(BtNodeType type, int parent = -1, int at = -1, float x = 0.0f, float y = 0.0f);
    /// Remove a node and everything under it.
    void RemoveSubtree(int id);
    /// Move a node (with its subtree) under another parent at sibling position `at`
    /// (-1 = last). Refuses to make a node its own descendant.
    bool Reparent(int id, int newParent, int at = -1);
    /// Put a node at another position among its siblings.
    void MoveChild(int id, int newIndex);
    /// Sort a node's flow children by their x position (after a drag).
    void SortChildrenByX(int id);
    /// Wrap a node in a new decorator (the decorator takes its place and parent).
    int InsertParent(int id, BtNodeType decorator);
    /// The children a composite / decorator runs (its services left out).
    std::vector<int> FlowChildren(int id) const;
    /// The services attached to a node.
    std::vector<int> Services(int id) const;
    bool IsAncestor(int ancestor, int id) const;
    int  Depth(int id) const;
    const Blackboard::Entry* Key(const std::string& n) const;
    void DeclareKey(const std::string& n, const BtValue& def);
    void RemoveKey(const std::string& n);
    void RenameKey(const std::string& from, const std::string& to);   ///< also in every node parameter naming it
    void Clear();
    int  NextId() const;
    /// The nodes in flow order (root first, children left to right, depth first).
    std::vector<int> Order() const;

    struct Issue { int node = -1; int severity = 1; std::string text; };   ///< 1 warning, 2 error
    /// Problems: an empty composite, a decorator without a child, a task with
    /// children, a parameter naming a blackboard key the tree lacks, waypoints or
    /// objects missing from `scene` (when one is given), no root.
    std::vector<Issue> Validate(const Scene* scene = nullptr) const;

    std::string Serialize() const;
    bool Deserialize(const std::string& text, std::string* error = nullptr);
    bool SaveToFile(const std::string& path, std::string* error = nullptr) const;
    bool LoadFromFile(const std::string& path, std::string* error = nullptr);

    /// Place every node top-down: the root at the top, children left to right.
    void Layout(float nodeW = 176.0f, float nodeH = 56.0f, float gapX = 22.0f, float gapY = 64.0f,
                float serviceRowH = 18.0f);
    /// The height of a node's card (a row per service).
    static float CardHeight(const BehaviorTree& t, int id, float nodeH = 56.0f, float serviceRowH = 18.0f);

    /// The "Guard" sample: patrol between the named waypoints, chase what the
    /// perception sees, attack in range, back to the patrol 5 s after losing it.
    static BehaviorTree GuardSample(const std::vector<std::string>& waypoints = {"Waypoint 1", "Waypoint 2", "Waypoint 3"},
                                    const std::string& targetName = "Player");

    /// Shared copies for runners: one load per file, dropped by Forget (the editor
    /// saved it) so the next runner reads the new file.
    static std::shared_ptr<const BehaviorTree> Load(const std::string& path, std::string* error = nullptr);
    static void Forget(const std::string& path);
    /// The project folder a relative tree path is looked for in (the editor and
    /// the player set it); ResolvePath applies it.
    static void SetAssetBaseDir(const std::string& dir);
    static const std::string& AssetBaseDir();
    static std::string ResolvePath(const std::string& path);
};

} // namespace okay
