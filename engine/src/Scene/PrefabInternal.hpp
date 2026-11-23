// Private to engine/src/Scene/PrefabTools.cpp and PrefabResolve.cpp: the prefab file reader and resolver (raw documents,
// object lists, deltas, nested prefabs and variants) that PrefabTools.cpp builds its public API on. Not installed.
#pragma once
#include "okay/Scene/PrefabTools.hpp"
#include "okay/Scene/Scene.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {
namespace prefab_text {

namespace fs = std::filesystem;
using Override = GameObject::PrefabOverride;

constexpr std::size_t kMaxObjects = 200000;                    // objects one prefab may expand to
constexpr std::uintmax_t kMaxFileBytes = 64ull * 1024 * 1024;  // refuse a bigger prefab file

// ---- Delta: overrides, unsets and removals ------------------------------------------

struct Unset { int object = 0; std::string record; int occurrence = 0; };

struct Delta {
    std::vector<Override> overrides;
    std::vector<Unset> unsets;
    std::vector<int> removes;
    bool Empty() const { return overrides.empty() && unsets.empty() && removes.empty(); }
};

// ---- Raw documents ------------------------------------------------------------------

struct RawObj {
    int idx = 0;                       // the number in its `gameobject` header
    std::string name;
    std::vector<std::string> lines;    // body lines as written, without the closing `end`
};

struct RawDoc {
    int version = 3;
    std::vector<std::string> head;     // top-level lines before the first object (variant, override...)
    std::vector<std::string> tail;     // top-level lines after it
    std::vector<RawObj> objs;
};

// What one raw object means.
struct ObjSem {
    int parent = -1;                      // header index of the parent
    bool hasNested = false;
    std::string nested;                   // nestedprefab "<file>"
    int under = -1;                       // attach point (variant / placeholder children)
    Delta delta;                          // a placeholder's override / unset / remove records
    std::vector<std::string> content;     // the object's own records, trimmed
    std::vector<std::string> problems;    // malformed lines
};

// ---- Resolved documents ----------------------------------------------------------------

// Where an object of a resolved document comes from, in terms of the top file.
struct Prov {
    enum Kind : char { None, Own, Base, Nested } kind = None;
    int a = -1;   // Own: position in the file's objects. Base: object index in the base.
                  // Nested: position of the nestedprefab object in the file's objects.
    int b = -1;   // Nested: object index inside the nested prefab.
};

struct RObj {
    std::string name;
    std::vector<std::string> lines;   // records, trimmed, no parent / link records
    int parent = -1;
    std::string link;                 // root of a nested instance: the file it is an instance of
    Prov prov;
    int attach = -1;                  // scratch: attach point while composing
};

struct RDoc {
    int version = 3;
    std::vector<RObj> objs;           // depth-first, objs[0] is the root
};

struct Diag {
    std::vector<PrefabIssue> issues;
    void Add(PrefabIssue::Severity s, const std::string& file, const std::string& msg) {
        for (const auto& i : issues) if (i.severity == s && i.file == file && i.message == msg) return;
        PrefabIssue is; is.severity = s; is.file = file; is.message = msg;
        issues.push_back(is);
    }
    void Warn(const std::string& file, const std::string& msg) { Add(PrefabIssue::Severity::Warning, file, msg); }
    void Error(const std::string& file, const std::string& msg) { Add(PrefabIssue::Severity::Error, file, msg); }
};

// ---- The resolver ------------------------------------------------------------------------

struct Baseline;

struct Ctx {
    int maxDepth = PrefabTools::kMaxDepth;
    std::vector<std::string> stackKeys, stackNames;
    std::map<std::string, std::shared_ptr<const RDoc>> okDocs;
    std::map<std::string, std::string> failed;
    std::map<std::string, std::shared_ptr<const Baseline>> baselines;
    std::map<std::string, std::string> textOverride;   // canonical path -> text to use instead of the file
    Diag diag;
    std::string fatal;                                 // cycle / depth / size: stops everything
    int skippedNested = 0;
};

enum class Status { Ok, Failed, Fatal };

Status ResolveFile(Ctx& ctx, const std::string& path, std::shared_ptr<const RDoc>& out, std::string& why);

struct StackGuard {
    Ctx& c;
    explicit StackGuard(Ctx& cx, const std::string& key, const std::string& name) : c(cx) {
        c.stackKeys.push_back(key); c.stackNames.push_back(name);
    }
    ~StackGuard() { c.stackKeys.pop_back(); c.stackNames.pop_back(); }
};

std::string Quote(const std::string& s);
std::string Trim(const std::string& s);
std::string FirstToken(const std::string& line);
void SkipSpaces(const std::string& s, std::size_t& p);
bool ReadQuoted(const std::string& s, std::size_t& p, std::string& out);
bool ReadInt(const std::string& s, std::size_t& p, int& v);
std::string BaseName(const std::string& path);
std::string Canon(const std::string& path);
std::string ResolveRef(const std::string& ref, const std::string& fromFile);
bool IsBookkeepingRecord(const std::string& tok);
bool IsStructuralToken(const std::string& tok);
std::string RecordKey(const std::string& line, std::unordered_map<std::string, int>& seen, int* occ = nullptr);
std::string Capitalized(std::string s);
int ParseDeltaLine(const std::string& t, Delta& d);
bool IsWholeTopRecord(const std::string& t);
bool ParseRaw(const std::string& text, RawDoc& doc, std::string& err);
std::string RawToText(const RawDoc& doc);
ObjSem Interpret(const RawObj& o);
std::string FormatIssue(const PrefabIssue& i);
std::vector<int> RemoveSubtrees(std::vector<RObj>& objs, std::vector<char> drop);
bool Normalize(std::vector<RObj>& objs, Diag& diag, const std::string& file);
std::vector<int> ApplyDelta(std::vector<RObj>& objs, const Delta& d, Diag& diag, const std::string& file, const std::string& what);
std::string Render(int version, const std::vector<RObj>& objs);
bool ReadPrefabText(Ctx& ctx, const std::string& path, const std::string& key, std::string& text, std::string& why);
Status ExpandOwn(Ctx& ctx, const RawDoc& raw, const std::vector<ObjSem>& sem, const std::string& file, std::vector<RObj>& out, int& version, std::string& why);
Status BuildDoc(Ctx& ctx, const RawDoc& raw, const std::string& file, RDoc& doc, std::string& why);

} // namespace prefab_text
} // namespace okay
