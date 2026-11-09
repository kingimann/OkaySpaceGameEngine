#pragma once
// Internal helpers shared by SceneDiff.cpp and SceneMerge.cpp. Not part of the public API.
#include "okay/Scene/SceneText.hpp"

#include <string>
#include <vector>

namespace okay {
namespace scenetext {

using Model = SceneText::Model;
using ModelObject = SceneText::ModelObject;
using Record = SceneText::Record;

/// "g:<guid>" for an object with an id, else "p:<path>".
std::string ObjectKey(const ModelObject& o);

struct Matching {
    std::vector<int> aToB;   ///< position in `b` of each object of `a`, or -1
    std::vector<int> bToA;
};
/// Pair up the objects of two versions of one scene: by guid; then, when one of the
/// two lacks a guid, by name path; then (renames) by identical content under the same
/// parent. Each object is in at most one pair.
Matching MatchObjects(const Model& a, const Model& b, bool detectRenames);

/// Which elements of `other` belong to a longest common subsequence with `base`
/// (both hold unique names; names missing from `base` never do). O(n log n).
std::vector<char> StableElements(const std::vector<std::string>& base, const std::vector<std::string>& other);

struct RecordPair {
    int a = -1;   ///< index in the first list, -1 when the record was added
    int b = -1;   ///< index in the second list, -1 when the record was removed
};
/// Pair the records of two versions of one object (or of the file-level records):
/// same record kind, identical text first, then in order. The result lists every
/// record of `b` in `b` order, then the records only `a` has in `a` order.
std::vector<RecordPair> AlignRecords(const std::vector<Record>& a, const std::vector<Record>& b);

/// Identity of a record inside an object: its kind, except that a `compoff` is its
/// whole text and an `override` is its object number plus the record it replaces.
std::string RecordIdentity(const Record& r);

struct FieldDelta {
    std::string name;      ///< "intensity", "[3]" or "" for a single-value record
    std::string before, after;
    bool hasBefore = true, hasAfter = true;
};
/// The per-field changes between two versions of one record (key=value records by
/// key, the rest by position, with field names where this file knows them).
std::vector<FieldDelta> DiffFields(const Record& a, const Record& b);

/// "Light" for `light`, "Transform" for `transform`; other kinds are shown as written.
std::string RecordLabel(const std::string& key);
/// The label of record `idx` of a list, with #n when the list has several of that kind.
std::string LabelAt(const std::vector<Record>& recs, std::size_t idx);
/// The name of field `i` (0 = first value after the record kind) of a known record, or "[i]".
std::string FieldName(const std::string& key, std::size_t i);

/// True when every value after the kind is `name=value` with distinct names.
bool IsKeyValueRecord(const std::vector<SceneText::Token>& toks);

/// A value as shown in a readable diff: newlines escaped, long text shortened.
std::string ShortValue(const std::string& v, std::size_t maxChars);
/// JSON string literal (with quotes) for arbitrary bytes.
std::string JsonString(const std::string& s);

} // namespace scenetext
} // namespace okay
