#include "okay/AI/BehaviorTree.hpp"

#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace okay {

const char* BtStatusName(BtStatus s) {
    switch (s) {
        case BtStatus::Running: return "Running";
        case BtStatus::Success: return "Success";
        case BtStatus::Failure: return "Failure";
        case BtStatus::Aborted: return "Aborted";
        default: return "-";
    }
}

// ---------------------------------------------------------------------------
// BtValue
// ---------------------------------------------------------------------------
namespace {

std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
bool ParseNumber(const std::string& s, float& out) {
    const std::string t = Trim(s);
    if (t.empty()) return false;
    char* end = nullptr;
    const float v = std::strtof(t.c_str(), &end);
    if (!end || *end != '\0') return false;
    out = v;
    return true;
}
bool ParseVector(const std::string& s, Vec3& out) {
    std::string t = s;
    for (char& c : t) if (c == ',' || c == '(' || c == ')') c = ' ';
    std::istringstream in(t);
    Vec3 v;
    if (!(in >> v.x >> v.y >> v.z)) return false;
    out = v;
    return true;
}
std::string NumText(float f) { return vs::VsValue::FormatNumber(f); }
std::string VecText(const Vec3& v) { return NumText(v.x) + " " + NumText(v.y) + " " + NumText(v.z); }

} // namespace

float BtValue::AsNumber() const {
    switch (type) {
        case Type::Number: return number;
        case Type::Bool:   return boolean ? 1.0f : 0.0f;
        case Type::String: { float f = 0.0f; return ParseNumber(text, f) ? f : 0.0f; }
        case Type::Vector: return vector.Magnitude();
        case Type::Object: return IsSet() ? 1.0f : 0.0f;
    }
    return 0.0f;
}
bool BtValue::AsBool() const {
    switch (type) {
        case Type::Number: return number != 0.0f;
        case Type::Bool:   return boolean;
        case Type::String: { const std::string l = Lower(Trim(text)); return !l.empty() && l != "0" && l != "false"; }
        case Type::Vector: return vector.x != 0.0f || vector.y != 0.0f || vector.z != 0.0f;
        case Type::Object: return IsSet();
    }
    return false;
}
std::string BtValue::AsString() const {
    switch (type) {
        case Type::Number: return NumText(number);
        case Type::Bool:   return boolean ? "true" : "false";
        case Type::String: return text;
        case Type::Vector: return VecText(vector);
        case Type::Object: {
            if (objectId != 0) if (GameObject* g = GameObject::FromInstanceId(objectId)) return g->name;
            return text;
        }
    }
    return {};
}
Vec3 BtValue::AsVector() const {
    switch (type) {
        case Type::Vector: return vector;
        case Type::Number: return Vec3{number, number, number};
        case Type::String: { Vec3 v; return ParseVector(text, v) ? v : Vec3{0, 0, 0}; }
        case Type::Object: {
            GameObject* g = objectId != 0 ? GameObject::FromInstanceId(objectId) : nullptr;
            return g && g->transform ? g->transform->Position() : Vec3{0, 0, 0};
        }
        default: return Vec3{0, 0, 0};
    }
}
GameObject* BtValue::AsObject(Scene* scene) const {
    if (type == Type::Object && objectId != 0)
        if (GameObject* g = GameObject::FromInstanceId(objectId)) return g;
    const std::string n = type == Type::Object ? text : AsString();
    return (scene && !n.empty()) ? scene->Find(n) : nullptr;
}
bool BtValue::operator==(const BtValue& o) const {
    if (type != o.type) return AsString() == o.AsString();
    switch (type) {
        case Type::Number: return number == o.number;
        case Type::Bool:   return boolean == o.boolean;
        case Type::String: return text == o.text;
        case Type::Vector: return vector.x == o.vector.x && vector.y == o.vector.y && vector.z == o.vector.z;
        case Type::Object: return (objectId != 0 && o.objectId != 0) ? objectId == o.objectId : AsString() == o.AsString();
    }
    return false;
}
std::string BtValue::ToText() const {
    switch (type) {
        case Type::Bool: return boolean ? "1" : "0";
        case Type::Object: return text;
        default: return AsString();
    }
}
BtValue BtValue::FromText(Type type, const std::string& s) {
    switch (type) {
        case Type::Number: { float f = 0.0f; ParseNumber(s, f); return Number(f); }
        case Type::Bool: { const std::string l = Lower(Trim(s)); return Bool(l == "1" || l == "true" || l == "yes" || l == "on"); }
        case Type::String: return String(s);
        case Type::Vector: { Vec3 v{0, 0, 0}; ParseVector(s, v); return Vector(v); }
        case Type::Object: return Object(Trim(s));
    }
    return {};
}
BtValue BtValue::Infer(const std::string& s) {
    const std::string t = Trim(s);
    float f = 0.0f;
    if (ParseNumber(t, f)) return Number(f);
    const std::string l = Lower(t);
    if (l == "true" || l == "false") return Bool(l == "true");
    Vec3 v;
    if (t.find(' ') != std::string::npos && ParseVector(t, v)) return Vector(v);
    return String(s);
}
vs::VsValue BtValue::ToVs() const {
    switch (type) {
        case Type::Number: return vs::VsValue(number);
        case Type::Bool:   return vs::VsValue(boolean);
        case Type::String: return vs::VsValue(text);
        case Type::Vector: return vs::VsValue(vector);
        case Type::Object: {
            if (objectId != 0 && GameObject::FromInstanceId(objectId)) return vs::VsValue::Object(objectId);
            return vs::VsValue(text);
        }
    }
    return {};
}
BtValue BtValue::FromVs(const vs::VsValue& v) {
    if (v.IsBool()) return Bool(v.AsBool());
    if (v.IsNumber()) return Number(v.AsFloat());
    if (v.IsVec3()) return Vector(v.AsVec3());
    if (v.IsObject()) {
        const std::uint64_t id = v.AsObjectId();
        GameObject* g = id ? GameObject::FromInstanceId(id) : nullptr;
        return Object(g ? g->name : std::string(), id);
    }
    return String(v.AsString());
}
BtValue BtValue::Converted(const BtValue& v) const {
    if (v.type == type) return v;
    switch (type) {
        case Type::Number: return Number(v.AsNumber());
        case Type::Bool:   return Bool(v.AsBool());
        case Type::String: return String(v.AsString());
        case Type::Vector: return Vector(v.AsVector());
        case Type::Object: return Object(v.AsString(), v.type == Type::Object ? v.objectId : 0);
    }
    return v;
}
const char* BtValue::TypeName(Type t) {
    switch (t) {
        case Type::Number: return "number";
        case Type::Bool:   return "bool";
        case Type::String: return "string";
        case Type::Vector: return "vector";
        case Type::Object: return "object";
    }
    return "number";
}
bool BtValue::TypeFromName(const std::string& n, Type& out) {
    const std::string l = Lower(Trim(n));
    if (l == "number" || l == "float" || l == "int") { out = Type::Number; return true; }
    if (l == "bool" || l == "boolean") { out = Type::Bool; return true; }
    if (l == "string" || l == "text") { out = Type::String; return true; }
    if (l == "vector" || l == "vec3" || l == "position") { out = Type::Vector; return true; }
    if (l == "object" || l == "gameobject") { out = Type::Object; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Blackboard
// ---------------------------------------------------------------------------
const BtValue* Blackboard::Find(const std::string& name) const {
    for (const Entry& e : m_entries) if (e.name == name) return &e.value;
    return nullptr;
}
BtValue* Blackboard::Find(const std::string& name) {
    for (Entry& e : m_entries) if (e.name == name) return &e.value;
    return nullptr;
}
void Blackboard::Set(const std::string& name, const BtValue& v) {
    if (name.empty()) return;
    ++m_version;
    if (BtValue* cur = Find(name)) { *cur = cur->Converted(v); return; }
    m_entries.push_back({name, v});
}
void Blackboard::Declare(const std::string& name, const BtValue& def) {
    if (name.empty()) return;
    ++m_version;
    if (BtValue* cur = Find(name)) { *cur = def; return; }
    m_entries.push_back({name, def});
}
void Blackboard::Remove(const std::string& name) {
    ++m_version;
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.name == name; }), m_entries.end());
}
float Blackboard::GetNumber(const std::string& name, float fallback) const { const BtValue* v = Find(name); return v ? v->AsNumber() : fallback; }
bool Blackboard::GetBool(const std::string& name, bool fallback) const { const BtValue* v = Find(name); return v ? v->AsBool() : fallback; }
std::string Blackboard::GetString(const std::string& name, const std::string& fallback) const { const BtValue* v = Find(name); return v ? v->AsString() : fallback; }
Vec3 Blackboard::GetVector(const std::string& name, const Vec3& fallback) const { const BtValue* v = Find(name); return v ? v->AsVector() : fallback; }
GameObject* Blackboard::GetGameObject(const std::string& name, Scene* scene) const { const BtValue* v = Find(name); return v ? v->AsObject(scene) : nullptr; }
bool Blackboard::GetPosition(const std::string& name, Scene* scene, Vec3& out) const {
    const BtValue* v = Find(name);
    if (!v) return false;
    if (v->type == BtValue::Type::Vector) { out = v->vector; return true; }
    if (GameObject* g = v->AsObject(scene); g && g->transform) { out = g->transform->Position(); return true; }
    return false;
}

// ---------------------------------------------------------------------------
// The node catalogue
// ---------------------------------------------------------------------------
namespace {

using K = BtParamDef::Kind;

// Condition / Guard share their test.
std::vector<BtParamDef> ConditionParams(bool guard) {
    std::vector<BtParamDef> p = {
        {"kind", "Test", K::Choice, "blackboard", "What is tested: a blackboard value, the distance to a target, a clear line of sight to it, or this NPC's health.", "blackboard|distance|lineOfSight|health"},
        {"key", "Key", K::Key, "", "The blackboard key compared (blackboard test).", nullptr},
        {"op", "Compare", K::Choice, "==", "How the key compares to the value; set / unset test whether an object key holds anything.", "==|!=|<|<=|>|>=|set|unset"},
        {"value", "Value", K::Text, "", "The value compared against, in the key's notation (3.5, true, a name).", nullptr},
        {"target", "Target", K::Target, "target", "An object or vector key, or a scene object's name (distance and line-of-sight tests).", nullptr},
        {"distance", "Distance", K::Number, "2", "True while the target is closer than this (distance test); the sight range of the line-of-sight test (0 = any).", nullptr},
        {"fraction", "Health Below", K::Number, "0.3", "True while health is below this fraction of the maximum (health test).", nullptr},
    };
    if (guard)
        p.push_back({"abort", "Abort", K::Choice, "both", "self: stop the subtree when the test turns false. lower: when the test turns true while a later sibling runs, stop that sibling and run this one. both: either.", "both|self|lower"});
    return p;
}

const std::vector<BtNodeDef>& Defs() {
    static const std::vector<BtNodeDef> d = {
        // ---- composites ----
        {BtNodeType::Sequence, "Sequence", "Sequence", BtNodeKind::Composite,
         "Runs its children left to right; stops at the first that fails (Failure), succeeds when all do.", {}},
        {BtNodeType::Selector, "Selector", "Selector", BtNodeKind::Composite,
         "Runs its children left to right until one succeeds (Success); fails when all fail.", {}},
        {BtNodeType::Parallel, "Parallel", "Parallel", BtNodeKind::Composite,
         "Runs every child at the same time. The policies say how many must succeed or fail to finish it.",
         {{"success", "Succeed When", K::Choice, "all", "all: every child succeeded. one: any child succeeded (the rest are stopped).", "all|one"},
          {"failure", "Fail When", K::Choice, "one", "one: any child failed (the rest are stopped). all: every child failed.", "one|all"}}},
        {BtNodeType::RandomSelector, "RandomSelector", "Random Selector", BtNodeKind::Composite,
         "A selector that tries its children in a random order each run; each child's Weight makes it more likely to go first.", {}},
        // ---- decorators ----
        {BtNodeType::Inverter, "Inverter", "Inverter", BtNodeKind::Decorator,
         "Success becomes Failure and the other way round.", {}},
        {BtNodeType::Succeeder, "Succeeder", "Succeeder", BtNodeKind::Decorator,
         "Always succeeds once its child finished, however it went.", {}},
        {BtNodeType::Repeat, "Repeat", "Repeat", BtNodeKind::Decorator,
         "Runs its child again when it finishes: a number of times (then Success), or forever.",
         {{"count", "Count", K::Int, "0", "How many runs (0 = forever).", nullptr}}},
        {BtNodeType::UntilFail, "UntilFail", "Until Fail", BtNodeKind::Decorator,
         "Runs its child again and again until it fails, then succeeds.", {}},
        {BtNodeType::Cooldown, "Cooldown", "Cooldown", BtNodeKind::Decorator,
         "After its child finished, fails for a while instead of running it again.",
         {{"seconds", "Seconds", K::Number, "2", "How long the child stays unavailable after it finished.", nullptr}}},
        {BtNodeType::TimeLimit, "TimeLimit", "Time Limit", BtNodeKind::Decorator,
         "Stops its child and fails when it runs longer than the limit.",
         {{"seconds", "Seconds", K::Number, "5", "The most the child may run.", nullptr}}},
        {BtNodeType::Condition, "Condition", "Condition", BtNodeKind::Decorator,
         "Runs its child only while the test holds (fails otherwise). Without a child it is the test itself.", ConditionParams(false)},
        {BtNodeType::Guard, "Guard", "Guard", BtNodeKind::Decorator,
         "A condition that keeps watching: it stops its subtree when the test turns false, and takes over from a later sibling when the test turns true.", ConditionParams(true)},
        // ---- tasks ----
        {BtNodeType::Wait, "Wait", "Wait", BtNodeKind::Task,
         "Runs for a number of seconds (a random one between Min and Max when Max is above Min), then succeeds.",
         {{"min", "Seconds", K::Number, "1", "How long to wait (the least, when Max is set).", nullptr},
          {"max", "Max", K::Number, "0", "The most to wait when the time is random (0 = exactly Seconds).", nullptr}}},
        {BtNodeType::MoveTo, "MoveTo", "Move To", BtNodeKind::Task,
         "Walks to a blackboard position or object (or the fixed Position) over the navigation. Succeeds at the stopping distance, fails when there is no way there.",
         {{"target", "Target", K::Target, "", "An object or vector key, or a scene object's name (empty: the Position below).", nullptr},
          {"position", "Position", K::Vector, "0 0 0", "A world point, used when Target is empty.", nullptr},
          {"stopDistance", "Stop Distance", K::Number, "0.5", "Done when this close.", nullptr},
          {"speed", "Speed", K::Number, "0", "Units per second (0 = the NPC's own walk speed).", nullptr},
          {"run", "Run", K::Bool, "0", "Use the NPC's run speed (NPC Controller).", nullptr}}},
        {BtNodeType::Patrol, "Patrol", "Patrol", BtNodeKind::Task,
         "Walks the named waypoint objects in turn, waiting at each. Keeps running (Once: succeeds after a full round).",
         {{"waypoints", "Waypoints", K::Waypoints, "", "Scene objects to visit, comma separated (Waypoint 1, Waypoint 2).", nullptr},
          {"mode", "Mode", K::Choice, "loop", "loop: A B C A. pingpong: A B C B A.", "loop|pingpong"},
          {"wait", "Wait", K::Number, "1", "Seconds to wait at each waypoint.", nullptr},
          {"stopDistance", "Stop Distance", K::Number, "0.6", "How close counts as reached.", nullptr},
          {"speed", "Speed", K::Number, "0", "Units per second (0 = the NPC's walk speed).", nullptr},
          {"once", "Once", K::Bool, "0", "Succeed after visiting every waypoint once.", nullptr}}},
        {BtNodeType::Wander, "Wander", "Wander", BtNodeKind::Task,
         "Walks to random points around the home point (on the navmesh when the scene has one), pausing between hops. Keeps running (Once: succeeds at the first point).",
         {{"radius", "Radius", K::Number, "6", "How far from home it roams.", nullptr},
          {"minPause", "Pause Min", K::Number, "0.5", "Seconds to pause at a point, at least.", nullptr},
          {"maxPause", "Pause Max", K::Number, "2", "Seconds to pause at a point, at most.", nullptr},
          {"speed", "Speed", K::Number, "0", "Units per second (0 = the NPC's walk speed).", nullptr},
          {"once", "Once", K::Bool, "0", "Succeed on reaching the first point.", nullptr}}},
        {BtNodeType::Chase, "Chase", "Chase", BtNodeKind::Task,
         "Follows a target as it moves, planning again on the way. Succeeds within the stopping distance, fails without a target or a way to it.",
         {{"target", "Target", K::Target, "target", "An object key or a scene object's name.", nullptr},
          {"stopDistance", "Stop Distance", K::Number, "1.4", "Done when this close (an attack range).", nullptr},
          {"speed", "Speed", K::Number, "0", "Units per second (0 = the NPC's speed).", nullptr},
          {"run", "Run", K::Bool, "1", "Use the NPC's run speed (NPC Controller).", nullptr},
          {"giveUp", "Give Up Past", K::Number, "0", "Fail when the target is farther than this (0 = never).", nullptr}}},
        {BtNodeType::Flee, "Flee", "Flee", BtNodeKind::Task,
         "Runs away from a target. Succeeds once it is Distance away, fails without a target.",
         {{"target", "Target", K::Target, "target", "An object key or a scene object's name.", nullptr},
          {"distance", "Safe Distance", K::Number, "10", "Done when this far from the target.", nullptr},
          {"speed", "Speed", K::Number, "0", "Units per second (0 = the NPC's speed).", nullptr},
          {"run", "Run", K::Bool, "1", "Use the NPC's run speed (NPC Controller).", nullptr}}},
        {BtNodeType::LookAt, "LookAt", "Look At", BtNodeKind::Task,
         "Turns to face a target (and looks at it with the head on a Character). Succeeds once facing it.",
         {{"target", "Target", K::Target, "target", "An object or vector key, or a scene object's name.", nullptr},
          {"turnSpeed", "Turn Speed", K::Number, "360", "Degrees per second (0 = snap).", nullptr},
          {"tolerance", "Tolerance", K::Number, "5", "Degrees off that still count as facing it.", nullptr},
          {"head", "Head Look", K::Bool, "1", "Also turn a Character's head toward the target.", nullptr}}},
        {BtNodeType::PlayAnimation, "PlayAnimation", "Play Animation", BtNodeKind::Task,
         "Plays a Character clip (or a built-in animation like Wave) and / or sets an Animator trigger or parameter.",
         {{"clip", "Clip", K::Text, "", "A Character clip or built-in animation name (empty: none).", nullptr},
          {"trigger", "Animator Trigger", K::Text, "", "An Animator trigger to set (empty: none).", nullptr},
          {"param", "Animator Param", K::Text, "", "An Animator float / bool parameter to set (empty: none).", nullptr},
          {"value", "Param Value", K::Text, "1", "The parameter's value (a number, or true / false).", nullptr},
          {"wait", "Wait For Clip", K::Bool, "0", "Keep running until the Character clip ends.", nullptr}}},
        {BtNodeType::Attack, "Attack", "Attack", BtNodeKind::Task,
         "Hits the target when in range: swings a Melee Attacker, else the NPC Controller's attack, else damages the target's health and sends the attack event. Fails out of range.",
         {{"target", "Target", K::Target, "target", "An object key or a scene object's name.", nullptr},
          {"range", "Range", K::Number, "0", "Reach (0 = the NPC Controller's attack range, else 1.6).", nullptr},
          {"damage", "Damage", K::Number, "0", "Health taken per hit (0 = the NPC Controller's damage, else 10).", nullptr},
          {"interval", "Interval", K::Number, "1", "Seconds between hits.", nullptr},
          {"windup", "Windup", K::Number, "0", "Seconds the hit is telegraphed before it lands.", nullptr}}},
        {BtNodeType::SetBlackboard, "SetBlackboard", "Set Blackboard", BtNodeKind::Task,
         "Writes a value into a blackboard key.",
         {{"key", "Key", K::Key, "", "The key written.", nullptr},
          {"value", "Value", K::Text, "", "The value, in the key's notation (3.5, true, a name, x y z).", nullptr}}},
        {BtNodeType::SendEvent, "SendEvent", "Send Event", BtNodeKind::Task,
         "Tells this object's scripts (on_bt_event(name)) and visual scripts (On Custom Event) that something happened; Broadcast reaches every object and the Action Lists.",
         {{"event", "Event", K::Event, "alert", "The event's name.", nullptr},
          {"target", "Target Object", K::Object, "", "Send it to this object instead (empty: this NPC).", nullptr},
          {"broadcast", "Broadcast", K::Bool, "0", "Send it to every script in the scene and the Action Lists.", nullptr}}},
        {BtNodeType::RunScript, "RunScript", "Run Script Function", BtNodeKind::Task,
         "Calls a function of this NPC's scripts. Fails when no script has it or it returns false.",
         {{"function", "Function", K::Function, "", "The function's name.", nullptr},
          {"arg", "Argument", K::Text, "", "Passed to the function when it takes one.", nullptr}}},
        {BtNodeType::PlayAudio, "PlaySound", "Play Sound", BtNodeKind::Task,
         "Plays this object's Audio Source (a file when given).",
         {{"clip", "File", K::File, "", "A sound file to play (empty: the Audio Source's clip).", nullptr},
          {"oneShot", "One Shot", K::Bool, "0", "Play on top of what is playing instead of restarting it.", nullptr},
          {"wait", "Wait", K::Bool, "0", "Keep running until the sound ends.", nullptr}}},
        {BtNodeType::StartDialogue, "StartDialogue", "Start Dialogue", BtNodeKind::Task,
         "Starts a conversation on this NPC's Dialogue Runner (a graph file when given) and, with Wait, runs until it ends. A conversation the player already started is waited on, not restarted. Fails without a Dialogue Runner or when the graph cannot start; an abort ends the conversation it started.",
         {{"graph", "Graph", K::File, "", "A .okaydialog to play (empty: the Dialogue Runner's own graph).", "*.okaydialog"},
          {"wait", "Wait", K::Bool, "1", "Keep running until the conversation ends.", nullptr}}},
        {BtNodeType::Custom, "Custom", "Custom", BtNodeKind::Task,
         "A task written in a script: the function is called every tick with \"enter\", \"tick\" or \"abort\" and returns \"running\", \"success\" or \"failure\".",
         {{"function", "Function", K::Function, "bt_task", "The script function that runs the task.", nullptr}}},
        // ---- services ----
        {BtNodeType::Perception, "Perception", "Perception", BtNodeKind::Service,
         "While its node runs: looks for the target in a sight cone (with a clear line of sight) and a hearing radius, and keeps the blackboard's target, can-see, last-seen position and seconds-since-seen keys up to date.",
         {{"targetName", "Target Object", K::Object, "Player", "The object looked for.", nullptr},
          {"range", "Sight Range", K::Number, "12", "How far it can see.", nullptr},
          {"angle", "Field Of View", K::Number, "120", "The sight cone, in degrees (360 = all around).", nullptr},
          {"lineOfSight", "Line Of Sight", K::Bool, "1", "Walls block sight (a physics ray from the eyes).", nullptr},
          {"eyeHeight", "Eye Height", K::Number, "1.2", "The eyes above the object's origin, for the sight ray.", nullptr},
          {"hearing", "Hearing Range", K::Number, "4", "Notices the target within this radius, even behind it (0 = deaf).", nullptr},
          {"interval", "Interval", K::Number, "0.1", "Seconds between checks (0 = every tick).", nullptr},
          {"targetKey", "Target Key", K::Key, "target", "Object key that receives the target.", "object"},
          {"seeKey", "Can See Key", K::Key, "canSee", "Bool key: the target is seen or heard right now.", "bool"},
          {"lastSeenKey", "Last Seen Key", K::Key, "lastSeen", "Vector key: where the target was last noticed.", "vector"},
          {"sinceKey", "Since Seen Key", K::Key, "sinceSeen", "Number key: seconds since the target was last noticed.", "number"}}},
        {BtNodeType::HealthWatcher, "HealthWatcher", "Health Watcher", BtNodeKind::Service,
         "While its node runs: copies this NPC's health (NPC Controller, Health Stat or Survival Stats) into the blackboard.",
         {{"interval", "Interval", K::Number, "0.2", "Seconds between checks (0 = every tick).", nullptr},
          {"healthKey", "Health Key", K::Key, "health", "Number key: the health points.", "number"},
          {"pctKey", "Fraction Key", K::Key, "healthPct", "Number key: health over maximum (0..1).", "number"},
          {"lowKey", "Low Key", K::Key, "lowHealth", "Bool key: health is below the threshold.", "bool"},
          {"threshold", "Low Threshold", K::Number, "0.3", "The fraction below which Low Key turns true.", nullptr}}},
    };
    return d;
}

} // namespace

const std::vector<BtNodeDef>& BtNodeDefs() { return Defs(); }
const BtNodeDef& BtDefOf(BtNodeType t) {
    const auto& d = Defs();
    const std::size_t i = (std::size_t)t;
    return i < d.size() ? d[i] : d.front();
}
bool BtTypeFromName(const std::string& name, BtNodeType& out) {
    for (const BtNodeDef& d : Defs())
        if (name == d.name) { out = d.type; return true; }
    const std::string l = Lower(name);
    for (const BtNodeDef& d : Defs())
        if (l == Lower(d.name) || l == Lower(d.title)) { out = d.type; return true; }
    return false;
}
const BtParamDef* BtParamOf(BtNodeType type, const std::string& key) {
    for (const BtParamDef& p : BtDefOf(type).params) if (key == p.key) return &p;
    return nullptr;
}

// ---------------------------------------------------------------------------
// BtNode
// ---------------------------------------------------------------------------
namespace {
std::string ParamText(const BtNode& n, const char* key) {
    auto it = n.params.find(key);
    if (it != n.params.end()) return it->second;
    const BtParamDef* p = BtParamOf(n.type, key);
    return p && p->def ? p->def : std::string();
}
} // namespace

float BtNode::Num(const char* key) const { float f = 0.0f; ParseNumber(ParamText(*this, key), f); return f; }
int BtNode::Int(const char* key) const { return (int)std::lround(Num(key)); }
bool BtNode::Flag(const char* key) const { return BtValue::FromText(BtValue::Type::Bool, ParamText(*this, key)).boolean; }
std::string BtNode::Str(const char* key) const { return ParamText(*this, key); }
Vec3 BtNode::Vec(const char* key) const { Vec3 v{0, 0, 0}; ParseVector(ParamText(*this, key), v); return v; }
void BtNode::Set(const char* key, float v) { params[key] = NumText(v); }
void BtNode::Set(const char* key, const Vec3& v) { params[key] = VecText(v); }

// ---------------------------------------------------------------------------
// BehaviorTree: structure
// ---------------------------------------------------------------------------
BtNode* BehaviorTree::Find(int id) { const int i = IndexOf(id); return i >= 0 ? &nodes[(std::size_t)i] : nullptr; }
const BtNode* BehaviorTree::Find(int id) const { const int i = IndexOf(id); return i >= 0 ? &nodes[(std::size_t)i] : nullptr; }
int BehaviorTree::IndexOf(int id) const {
    for (std::size_t i = 0; i < nodes.size(); ++i) if (nodes[i].id == id) return (int)i;
    return -1;
}
int BehaviorTree::NextId() const {
    int n = 0;
    for (const BtNode& b : nodes) n = std::max(n, b.id + 1);
    return n;
}
int BehaviorTree::AddNode(BtNodeType type, int parent, int at, float x, float y) {
    BtNode n;
    n.id = NextId(); n.type = type; n.parent = -1; n.x = x; n.y = y;
    nodes.push_back(n);
    const int id = n.id;
    if (parent >= 0 && Find(parent)) {
        Reparent(id, parent, at);
    } else if (root < 0 || !Find(root)) {
        root = id;
    }
    return id;
}
void BehaviorTree::RemoveSubtree(int id) {
    BtNode* n = Find(id);
    if (!n) return;
    const std::vector<int> kids = n->children;
    for (int k : kids) RemoveSubtree(k);
    if (n = Find(id); n) {
        if (BtNode* p = Find(n->parent)) p->children.erase(std::remove(p->children.begin(), p->children.end(), id), p->children.end());
        nodes.erase(nodes.begin() + IndexOf(id));
    }
    if (root == id) root = -1;
}
bool BehaviorTree::IsAncestor(int ancestor, int id) const {
    for (const BtNode* n = Find(id); n && n->parent >= 0; n = Find(n->parent))
        if (n->parent == ancestor) return true;
    return false;
}
bool BehaviorTree::Reparent(int id, int newParent, int at) {
    BtNode* n = Find(id);
    if (!n || id == newParent) return false;
    if (newParent >= 0 && (!Find(newParent) || IsAncestor(id, newParent))) return false;
    if (BtNode* old = Find(n->parent)) old->children.erase(std::remove(old->children.begin(), old->children.end(), id), old->children.end());
    n->parent = newParent;
    if (BtNode* p = Find(newParent)) {
        if (at < 0 || at >= (int)p->children.size()) p->children.push_back(id);
        else p->children.insert(p->children.begin() + at, id);
    }
    if (root == id && newParent >= 0) root = -1;
    if (root < 0 && newParent < 0) root = id;
    return true;
}
void BehaviorTree::MoveChild(int id, int newIndex) {
    const BtNode* n = Find(id);
    BtNode* p = n ? Find(n->parent) : nullptr;
    if (!p) return;
    auto it = std::find(p->children.begin(), p->children.end(), id);
    if (it == p->children.end()) return;
    p->children.erase(it);
    newIndex = std::clamp(newIndex, 0, (int)p->children.size());
    p->children.insert(p->children.begin() + newIndex, id);
}
void BehaviorTree::SortChildrenByX(int id) {
    BtNode* p = Find(id);
    if (!p) return;
    std::stable_sort(p->children.begin(), p->children.end(), [&](int a, int b) {
        const BtNode* na = Find(a); const BtNode* nb = Find(b);
        const bool sa = na && BtIsService(na->type), sb = nb && BtIsService(nb->type);
        if (sa != sb) return sa;   // services first, in their order
        if (sa) return false;
        return (na ? na->x : 0.0f) < (nb ? nb->x : 0.0f);
    });
}
int BehaviorTree::InsertParent(int id, BtNodeType decorator) {
    const BtNode* n = Find(id);
    if (!n) return -1;
    const int parent = n->parent;
    int at = -1;
    if (const BtNode* p = Find(parent)) {
        auto it = std::find(p->children.begin(), p->children.end(), id);
        if (it != p->children.end()) at = (int)(it - p->children.begin());
    }
    const float x = n->x, y = n->y;
    const int d = AddNode(decorator, -1, -1, x, y);
    if (parent >= 0) Reparent(d, parent, at);
    else if (root == id) root = d;
    Reparent(id, d, -1);
    return d;
}
std::vector<int> BehaviorTree::FlowChildren(int id) const {
    std::vector<int> out;
    if (const BtNode* n = Find(id))
        for (int c : n->children) if (const BtNode* k = Find(c); k && !BtIsService(k->type)) out.push_back(c);
    return out;
}
std::vector<int> BehaviorTree::Services(int id) const {
    std::vector<int> out;
    if (const BtNode* n = Find(id))
        for (int c : n->children) if (const BtNode* k = Find(c); k && BtIsService(k->type)) out.push_back(c);
    return out;
}
int BehaviorTree::Depth(int id) const {
    int d = 0;
    for (const BtNode* n = Find(id); n && n->parent >= 0; n = Find(n->parent)) ++d;
    return d;
}
const Blackboard::Entry* BehaviorTree::Key(const std::string& n) const {
    for (const auto& k : keys) if (k.name == n) return &k;
    return nullptr;
}
void BehaviorTree::DeclareKey(const std::string& n, const BtValue& def) {
    if (n.empty()) return;
    for (auto& k : keys) if (k.name == n) { k.value = def; return; }
    keys.push_back({n, def});
}
void BehaviorTree::RemoveKey(const std::string& n) {
    keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const Blackboard::Entry& e) { return e.name == n; }), keys.end());
}
void BehaviorTree::RenameKey(const std::string& from, const std::string& to) {
    if (from.empty() || to.empty() || from == to) return;
    for (auto& k : keys) if (k.name == from) k.name = to;
    for (BtNode& n : nodes)
        for (const BtParamDef& p : n.Def().params) {
            if (p.kind != K::Key && p.kind != K::Target) continue;
            auto it = n.params.find(p.key);
            if (it != n.params.end() && it->second == from) it->second = to;
            else if (it == n.params.end() && p.def && from == p.def) n.params[p.key] = to;
        }
}
void BehaviorTree::Clear() { name.clear(); keys.clear(); nodes.clear(); root = -1; }
std::vector<int> BehaviorTree::Order() const {
    std::vector<int> out;
    std::vector<char> seen(nodes.size(), 0);
    std::function<void(int)> visit = [&](int id) {
        const int i = IndexOf(id);
        if (i < 0 || seen[(std::size_t)i]) return;
        seen[(std::size_t)i] = 1;
        out.push_back(id);
        for (int c : nodes[(std::size_t)i].children) visit(c);
    };
    if (root >= 0) visit(root);
    for (const BtNode& n : nodes) if (n.parent < 0) visit(n.id);
    for (const BtNode& n : nodes) visit(n.id);
    return out;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
std::vector<BehaviorTree::Issue> BehaviorTree::Validate(const Scene* scene) const {
    std::vector<Issue> out;
    auto add = [&](int node, int sev, std::string text) { out.push_back({node, sev, std::move(text)}); };
    if (root < 0 || !Find(root)) { add(-1, 2, "The tree has no root node"); }
    // Which nodes the root reaches (the rest never run).
    std::vector<char> reach(nodes.size(), 0);
    {
        std::function<void(int)> visit = [&](int id) {
            const int i = IndexOf(id);
            if (i < 0 || reach[(std::size_t)i]) return;
            reach[(std::size_t)i] = 1;
            for (int c : nodes[(std::size_t)i].children) visit(c);
        };
        if (root >= 0) visit(root);
    }
    auto hasObject = [&](const std::string& n) { return !scene || scene->Find(n) != nullptr; };
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const BtNode& n = nodes[i];
        const std::string label = n.Def().title;
        if (!reach[i]) add(n.id, 1, label + " is not connected to the root (it never runs)");
        const std::vector<int> flow = FlowChildren(n.id);
        switch (n.Kind()) {
            case BtNodeKind::Composite:
                if (flow.empty()) add(n.id, 2, label + " has no children");
                break;
            case BtNodeKind::Decorator:
                if (flow.empty() && n.type != BtNodeType::Condition) add(n.id, 2, label + " has no child");
                if (flow.size() > 1) add(n.id, 2, label + " can only have one child (it has " + std::to_string(flow.size()) + ")");
                break;
            case BtNodeKind::Task:
                if (!flow.empty()) add(n.id, 1, label + " is a task: its children never run");
                break;
            case BtNodeKind::Service:
                if (!n.children.empty()) add(n.id, 2, label + " is a service: it cannot have children");
                if (n.parent < 0) add(n.id, 2, label + " must be attached to a node");
                break;
        }
        for (const BtParamDef& p : n.Def().params) {
            const std::string v = n.Str(p.key);
            switch (p.kind) {
                case K::Key:
                    if (!v.empty() && !Key(v)) add(n.id, 2, label + ": no blackboard key named '" + v + "'");
                    else if (v.empty() && (n.type == BtNodeType::SetBlackboard || ((n.type == BtNodeType::Condition || n.type == BtNodeType::Guard) && n.Str("kind") == "blackboard")))
                        add(n.id, 2, label + " names no blackboard key");
                    else if (!v.empty() && p.choices) {
                        const Blackboard::Entry* k = Key(v);
                        if (k && std::string(p.choices).find(BtValue::TypeName(k->value.type)) == std::string::npos)
                            add(n.id, 1, label + ": key '" + v + "' is a " + BtValue::TypeName(k->value.type) + ", " + p.label + " expects " + p.choices);
                    }
                    break;
                case K::Target: {
                    const bool used = n.type != BtNodeType::Condition && n.type != BtNodeType::Guard ? true : n.Str("kind") != "blackboard" && n.Str("kind") != "health";
                    if (!used) break;
                    if (v.empty()) { if (n.type != BtNodeType::MoveTo) add(n.id, 2, label + " has no target"); }
                    else if (!Key(v) && !hasObject(v)) add(n.id, 1, label + ": '" + v + "' is neither a blackboard key nor an object in the scene");
                    else if (const Blackboard::Entry* k = Key(v); k && k->value.type != BtValue::Type::Object && k->value.type != BtValue::Type::Vector)
                        add(n.id, 1, label + ": key '" + v + "' is a " + BtValue::TypeName(k->value.type) + ", not an object or vector");
                    break;
                }
                case K::Object:
                    if (!v.empty() && !hasObject(v)) add(n.id, 1, label + ": no object named '" + v + "' in the scene");
                    break;
                case K::Waypoints: {
                    int count = 0;
                    std::stringstream ss(v);
                    std::string item;
                    while (std::getline(ss, item, ',')) {
                        item = Trim(item);
                        if (item.empty()) continue;
                        ++count;
                        if (!hasObject(item)) add(n.id, 1, label + ": missing waypoint '" + item + "'");
                    }
                    if (count == 0) add(n.id, 2, label + " has no waypoints");
                    break;
                }
                case K::Function:
                case K::Event:
                    if (v.empty()) add(n.id, 2, label + " names no " + (p.kind == K::Function ? "function" : "event"));
                    break;
                default: break;
            }
        }
        if (n.type == BtNodeType::Wait && n.Num("min") < 0.0f) add(n.id, 1, "Wait: a negative time");
        if (n.type == BtNodeType::PlayAnimation && n.Str("clip").empty() && n.Str("trigger").empty() && n.Str("param").empty())
            add(n.id, 1, "Play Animation plays nothing (no clip, trigger or parameter)");
        if (n.parent >= 0 && Find(n.parent) && Find(n.parent)->type == BtNodeType::RandomSelector && !BtIsService(n.type) && n.Num("weight") < 0.0f)
            add(n.id, 1, label + ": a negative weight");
    }
    std::stable_sort(out.begin(), out.end(), [](const Issue& a, const Issue& b) { return a.severity > b.severity; });
    return out;
}

// ---------------------------------------------------------------------------
// Text format
// ---------------------------------------------------------------------------
namespace {

bool NeedsQuotes(const std::string& s) {
    if (s.empty()) return true;
    for (char c : s) if (std::isspace((unsigned char)c) || c == '"' || c == '\\' || c == '=' || c == '#') return true;
    return false;
}
std::string Quote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o + "\"";
}
std::string Atom(const std::string& s) { return NeedsQuotes(s) ? Quote(s) : s; }

// Split a line into tokens; quoted tokens keep their spaces. A token may be
// `key=value` with a quoted value.
std::vector<std::string> Tokens(const std::string& line) {
    std::vector<std::string> out;
    std::size_t i = 0;
    auto readQuoted = [&](std::string& into) {
        ++i;   // the opening quote
        while (i < line.size() && line[i] != '"') {
            if (line[i] == '\\' && i + 1 < line.size()) {
                ++i;
                into += line[i] == 'n' ? '\n' : line[i];
            } else into += line[i];
            ++i;
        }
        if (i < line.size()) ++i;   // the closing quote
    };
    while (i < line.size()) {
        while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
        if (i >= line.size()) break;
        if (line[i] == '#') break;
        std::string tok;
        if (line[i] == '"') readQuoted(tok);
        else {
            while (i < line.size() && !std::isspace((unsigned char)line[i])) {
                tok += line[i];
                ++i;
                if (tok.back() == '=' && i < line.size() && line[i] == '"') { readQuoted(tok); break; }
            }
        }
        out.push_back(tok);
    }
    return out;
}

} // namespace

std::string BehaviorTree::Serialize() const {
    std::ostringstream out;
    out << "okaybt 1\n";
    out << "name " << Atom(name) << "\n";
    for (const auto& k : keys)
        out << "key " << Atom(k.name) << " " << BtValue::TypeName(k.value.type) << " " << Atom(k.value.ToText()) << "\n";
    if (root >= 0) out << "root " << root << "\n";
    for (int id : Order()) {
        const BtNode& n = *Find(id);
        out << "node " << n.id << " " << n.Def().name << " parent=" << n.parent
            << " x=" << NumText(std::round(n.x)) << " y=" << NumText(std::round(n.y));
        if (!n.name.empty()) out << " name=" << Quote(n.name);
        for (const auto& kv : n.params) out << " " << kv.first << "=" << Atom(kv.second);
        out << "\n";
    }
    return out.str();
}

bool BehaviorTree::Deserialize(const std::string& text, std::string* error) {
    Clear();
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    bool header = false;
    int explicitRoot = -1;
    std::vector<std::pair<int, int>> parents;   // node id, parent id (in file order)
    auto fail = [&](const std::string& m) {
        if (error) *error = "line " + std::to_string(lineNo) + ": " + m;
        Clear();
        return false;
    };
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::vector<std::string> t = Tokens(line);
        if (t.empty()) continue;
        if (!header) {
            if (t[0] != "okaybt") return fail("not a behavior tree file (expected 'okaybt 1')");
            header = true;
            continue;
        }
        if (t[0] == "name") { name = t.size() > 1 ? t[1] : std::string(); }
        else if (t[0] == "key") {
            if (t.size() < 3) return fail("key needs a name and a type");
            BtValue::Type ty;
            if (!BtValue::TypeFromName(t[2], ty)) return fail("unknown key type '" + t[2] + "'");
            keys.push_back({t[1], BtValue::FromText(ty, t.size() > 3 ? t[3] : std::string())});
        } else if (t[0] == "root") {
            if (t.size() < 2) return fail("root needs a node id");
            explicitRoot = std::atoi(t[1].c_str());
        } else if (t[0] == "node") {
            if (t.size() < 3) return fail("node needs an id and a type");
            BtNode n;
            n.id = std::atoi(t[1].c_str());
            if (!BtTypeFromName(t[2], n.type)) return fail("unknown node type '" + t[2] + "'");
            if (Find(n.id)) return fail("node id " + std::to_string(n.id) + " used twice");
            int parent = -1;
            for (std::size_t i = 3; i < t.size(); ++i) {
                const std::size_t eq = t[i].find('=');
                if (eq == std::string::npos) continue;
                const std::string k = t[i].substr(0, eq), v = t[i].substr(eq + 1);
                if (k == "parent") parent = std::atoi(v.c_str());
                else if (k == "x") n.x = std::strtof(v.c_str(), nullptr);
                else if (k == "y") n.y = std::strtof(v.c_str(), nullptr);
                else if (k == "name") n.name = v;
                else n.params[k] = v;
            }
            n.parent = parent;
            nodes.push_back(n);
            parents.push_back({n.id, parent});
        } else return fail("unexpected '" + t[0] + "'");
    }
    if (!header) return fail("empty file");
    for (const auto& pp : parents) {
        BtNode* p = Find(pp.second);
        if (pp.second >= 0 && !p) return fail("node " + std::to_string(pp.first) + " names a parent that does not exist");
        if (p) p->children.push_back(pp.first);
    }
    root = -1;
    if (explicitRoot >= 0 && Find(explicitRoot)) root = explicitRoot;
    else for (const BtNode& n : nodes) if (n.parent < 0) { root = n.id; break; }
    // A cycle in the parents would hang every walk: make sure each node reaches a root.
    for (const BtNode& n : nodes) {
        int steps = 0;
        for (const BtNode* c = &n; c && c->parent >= 0; c = Find(c->parent))
            if (++steps > (int)nodes.size()) return fail("node " + std::to_string(n.id) + " is its own ancestor");
    }
    return true;
}

bool BehaviorTree::SaveToFile(const std::string& path, std::string* error) const {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { if (error) *error = "cannot write " + path; return false; }
    f << Serialize();
    if (!f) { if (error) *error = "write failed: " + path; return false; }
    return true;
}
bool BehaviorTree::LoadFromFile(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot read " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return Deserialize(ss.str(), error);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
float BehaviorTree::CardHeight(const BehaviorTree& t, int id, float nodeH, float serviceRowH) {
    return nodeH + (float)t.Services(id).size() * serviceRowH;
}
void BehaviorTree::Layout(float nodeW, float nodeH, float gapX, float gapY, float serviceRowH) {
    if (root < 0 || !Find(root)) return;
    std::unordered_map<int, float> width;
    std::function<float(int)> measure = [&](int id) -> float {
        float w = 0.0f;
        const std::vector<int> kids = FlowChildren(id);
        for (std::size_t i = 0; i < kids.size(); ++i) w += measure(kids[i]) + (i ? gapX : 0.0f);
        w = std::max(w, nodeW);
        width[id] = w;
        return w;
    };
    measure(root);
    // Row heights: the tallest card on each depth.
    std::vector<float> rowH;
    std::function<void(int, int)> rows = [&](int id, int depth) {
        if ((int)rowH.size() <= depth) rowH.resize((std::size_t)depth + 1, nodeH);
        rowH[(std::size_t)depth] = std::max(rowH[(std::size_t)depth], CardHeight(*this, id, nodeH, serviceRowH));
        for (int c : FlowChildren(id)) rows(c, depth + 1);
    };
    rows(root, 0);
    std::vector<float> rowY(rowH.size(), 0.0f);
    for (std::size_t i = 1; i < rowH.size(); ++i) rowY[i] = rowY[i - 1] + rowH[i - 1] + gapY;
    std::function<void(int, float, int)> place = [&](int id, float left, int depth) {
        BtNode* n = Find(id);
        const float w = width[id];
        n->x = left + (w - nodeW) * 0.5f;
        n->y = rowY[(std::size_t)depth];
        float cx = left;
        for (int c : FlowChildren(id)) { place(c, cx, depth + 1); cx += width[c] + gapX; }
        for (int s : Services(id)) if (BtNode* sv = Find(s)) { sv->x = n->x; sv->y = n->y; }
    };
    place(root, 0.0f, 0);
    // Free nodes (not under the root) go in a row below everything.
    float fx = 0.0f;
    const float fy = rowY.empty() ? 0.0f : rowY.back() + rowH.back() + gapY;
    for (BtNode& n : nodes)
        if (n.parent < 0 && n.id != root) { n.x = fx; n.y = fy; fx += nodeW + gapX; }
}

// ---------------------------------------------------------------------------
// The Guard sample
// ---------------------------------------------------------------------------
BehaviorTree BehaviorTree::GuardSample(const std::vector<std::string>& waypoints, const std::string& targetName) {
    BehaviorTree t;
    t.name = "Guard";
    t.DeclareKey("target", BtValue::Object(""));
    t.DeclareKey("canSee", BtValue::Bool(false));
    t.DeclareKey("lastSeen", BtValue::Vector({0, 0, 0}));
    t.DeclareKey("sinceSeen", BtValue::Number(9999.0f));
    t.DeclareKey("health", BtValue::Number(100.0f));
    t.DeclareKey("healthPct", BtValue::Number(1.0f));
    t.DeclareKey("lowHealth", BtValue::Bool(false));

    const int rootId = t.AddNode(BtNodeType::Selector, -1);
    t.Find(rootId)->name = "Guard";
    {
        const int per = t.AddNode(BtNodeType::Perception, rootId);
        BtNode* p = t.Find(per);
        p->Set("targetName", targetName); p->Set("range", 12.0f); p->Set("angle", 120.0f); p->Set("hearing", 4.0f);
        t.AddNode(BtNodeType::HealthWatcher, rootId);
    }
    // Seen within the last 5 s: hunt. The guard takes over from the patrol when
    // the target shows up and lets go 5 s after it was last noticed.
    const int hunt = t.AddNode(BtNodeType::Guard, rootId);
    {
        BtNode* g = t.Find(hunt);
        g->name = "Seen recently";
        g->Set("kind", "blackboard"); g->Set("key", "sinceSeen"); g->Set("op", "<"); g->Set("value", "5"); g->Set("abort", "both");
    }
    const int huntSel = t.AddNode(BtNodeType::Selector, hunt);
    t.Find(huntSel)->name = "Hunt";
    const int attackSeq = t.AddNode(BtNodeType::Sequence, huntSel);
    t.Find(attackSeq)->name = "In range";
    {
        const int inRange = t.AddNode(BtNodeType::Condition, attackSeq);
        BtNode* c = t.Find(inRange);
        c->name = "Close enough";
        c->Set("kind", "distance"); c->Set("target", "target"); c->Set("distance", 1.8f);
        const int look = t.AddNode(BtNodeType::LookAt, attackSeq);
        t.Find(look)->Set("target", "target");
        const int atk = t.AddNode(BtNodeType::Attack, attackSeq);
        BtNode* a = t.Find(atk);
        a->Set("target", "target"); a->Set("range", 2.0f); a->Set("damage", 8.0f); a->Set("interval", 1.0f);
    }
    {
        const int chase = t.AddNode(BtNodeType::Chase, huntSel);
        BtNode* c = t.Find(chase);
        c->Set("target", "target"); c->Set("stopDistance", 1.4f); c->Set("run", true);
    }
    {
        const int patrol = t.AddNode(BtNodeType::Patrol, rootId);
        BtNode* p = t.Find(patrol);
        p->name = "Patrol";
        std::string wp;
        for (const std::string& w : waypoints) { if (!wp.empty()) wp += ", "; wp += w; }
        p->Set("waypoints", wp); p->Set("mode", "loop"); p->Set("wait", 1.0f);
    }
    t.Layout();
    return t;
}

// ---------------------------------------------------------------------------
// Shared loads and the asset folder
// ---------------------------------------------------------------------------
namespace {
std::string& BaseDir() { static std::string d; return d; }
std::mutex& CacheMutex() { static std::mutex m; return m; }
std::unordered_map<std::string, std::shared_ptr<const BehaviorTree>>& Cache() {
    static std::unordered_map<std::string, std::shared_ptr<const BehaviorTree>> c;
    return c;
}
} // namespace

void BehaviorTree::SetAssetBaseDir(const std::string& dir) { BaseDir() = dir; }
const std::string& BehaviorTree::AssetBaseDir() { return BaseDir(); }
std::string BehaviorTree::ResolvePath(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.empty()) return path;
    const fs::path p(path);
    if (p.is_absolute() || BaseDir().empty() || fs::exists(p, ec)) return path;
    const fs::path under = fs::path(BaseDir()) / p;
    if (fs::exists(under, ec)) return under.string();
    const fs::path assets = fs::path(BaseDir()) / "Assets" / p;
    if (fs::exists(assets, ec)) return assets.string();
    return under.string();
}
std::shared_ptr<const BehaviorTree> BehaviorTree::Load(const std::string& path, std::string* error) {
    const std::string full = ResolvePath(path);
    {
        std::lock_guard<std::mutex> lock(CacheMutex());
        auto it = Cache().find(full);
        if (it != Cache().end()) return it->second;
    }
    auto t = std::make_shared<BehaviorTree>();
    if (!t->LoadFromFile(full, error)) return nullptr;
    std::lock_guard<std::mutex> lock(CacheMutex());
    Cache()[full] = t;
    return t;
}
void BehaviorTree::Forget(const std::string& path) {
    std::lock_guard<std::mutex> lock(CacheMutex());
    if (path.empty()) { Cache().clear(); return; }
    Cache().erase(ResolvePath(path));
    Cache().erase(path);
}

} // namespace okay
