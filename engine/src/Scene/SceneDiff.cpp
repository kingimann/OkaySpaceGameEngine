#include "okay/Scene/SceneDiff.hpp"
#include "SceneTextDetail.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace okay {

using namespace scenetext;

namespace {

Record MakeRecord(const std::string& text) { return Record{SceneText::KeyOf(text), text}; }

// Diff two record lists (an object's, or the file-level ones). Fills `changes`, the
// layout of the second list, and says whether the order of the kept records changed.
void DiffRecords(const std::vector<Record>& a, const std::vector<Record>& b,
                 std::vector<SceneDiff::RecordChange>& changes,
                 std::vector<SceneDiff::ObjectChange::Slot>& layout, bool& reordered) {
    const auto pairs = AlignRecords(a, b);
    int lastA = -1;
    reordered = false;
    for (const RecordPair& p : pairs) {
        if (p.b < 0) {
            SceneDiff::RecordChange c;
            c.kind = SceneDiff::RecordChange::Kind::Removed;
            c.label = LabelAt(a, (std::size_t)p.a);
            c.before = a[(std::size_t)p.a].text;
            changes.push_back(std::move(c));
            continue;
        }
        const Record& rb = b[(std::size_t)p.b];
        SceneDiff::ObjectChange::Slot slot;
        if (p.a < 0) {
            SceneDiff::RecordChange c;
            c.kind = SceneDiff::RecordChange::Kind::Added;
            c.label = LabelAt(b, (std::size_t)p.b);
            c.after = rb.text;
            changes.push_back(std::move(c));
            slot.text = rb.text;
        } else {
            const Record& ra = a[(std::size_t)p.a];
            if (p.a < lastA) reordered = true;
            lastA = std::max(lastA, p.a);
            if (ra.text == rb.text) slot.baseIndex = p.a;
            else {
                SceneDiff::RecordChange c;
                c.kind = SceneDiff::RecordChange::Kind::Modified;
                c.label = LabelAt(b, (std::size_t)p.b);
                c.before = ra.text;
                c.after = rb.text;
                for (const FieldDelta& f : DiffFields(ra, rb))
                    c.fields.push_back({f.name, f.before, f.after, f.hasBefore, f.hasAfter});
                changes.push_back(std::move(c));
                slot.text = rb.text;
            }
        }
        layout.push_back(std::move(slot));
    }
}

} // namespace

SceneDiff::Result SceneDiff::Compute(const std::string& baseText, const std::string& otherText) {
    return Compute(baseText, otherText, Options());
}

SceneDiff::Result SceneDiff::Compute(const std::string& baseText, const std::string& otherText, const Options& opts) {
    Result r;
    Model A, B;
    std::string err;
    // Canonical text first: files that differ only in layout (line endings, blanks,
    // number formatting, object numbering) compare equal.
    if (!SceneText::BuildModel(SceneText::Canonicalize(baseText), A, &err)) { r.error = "base: " + err; return r; }
    if (!SceneText::BuildModel(SceneText::Canonicalize(otherText), B, &err)) { r.error = "other: " + err; return r; }
    r.ok = true;
    r.baseVersion = A.version;
    r.otherVersion = B.version;
    for (const auto& w : A.warnings) r.warnings.push_back("base: " + w);
    for (const auto& w : B.warnings) r.warnings.push_back("other: " + w);
    for (int side = 0; side < 2; ++side) {
        const int v = side == 0 ? A.version : B.version;
        if (v > SceneText::CurrentVersion())
            r.warnings.push_back(std::string(side == 0 ? "base" : "other") + " is okayscene " + std::to_string(v) +
                                 ", newer than this build reads (" + std::to_string(SceneText::CurrentVersion()) +
                                 "): compared as text, records this build does not know are shown as written");
    }

    const std::size_t na = A.objects.size(), nb = B.objects.size();
    const Matching mt = MatchObjects(A, B, opts.detectRenames);
    std::vector<std::string> kA(na), kB(nb);
    std::unordered_set<std::string> used;
    for (std::size_t i = 0; i < na; ++i) { kA[i] = ObjectKey(A.objects[i]); used.insert(kA[i]); }
    for (std::size_t j = 0; j < nb; ++j) {
        if (mt.bToA[j] >= 0) { kB[j] = kA[(std::size_t)mt.bToA[j]]; continue; }
        std::string k = ObjectKey(B.objects[j]);
        while (used.count(k)) k += "+";
        used.insert(k);
        kB[j] = k;
    }
    auto parentKeyOf = [](const Model& m, const std::vector<std::string>& k, std::size_t i) {
        const int p = m.objects[i].parent;
        return p < 0 ? std::string() : k[(std::size_t)p];
    };
    auto parentPathOf = [](const Model& m, std::size_t i) {
        const int p = m.objects[i].parent;
        return p < 0 ? std::string() : m.objects[(std::size_t)p].path;
    };

    // File-level records.
    {
        bool reordered = false;
        DiffRecords(A.globals, B.globals, r.globals, r.globalLayout, reordered);
        // A reordering of file-level records is not worth reporting: the layout carries it.
    }

    // Objects of the other file, in file order.
    std::vector<SceneDiff::ObjectChange> chB(nb);
    std::vector<char> changed(nb, 0);
    for (std::size_t j = 0; j < nb; ++j) {
        ObjectChange& c = chB[j];
        const ModelObject& ob = B.objects[j];
        c.key = kB[j];
        c.guid = ob.guid;
        c.name = ob.name;
        c.path = ob.path;
        c.parentKey = parentKeyOf(B, kB, j);
        c.newGuid = ob.guid;
        c.newHeaderTail = ob.headerTail;
        if (mt.bToA[j] < 0) {
            c.kind = ObjectChange::Kind::Added;
            c.newParentPath = parentPathOf(B, j);
            for (std::size_t k = 0; k < ob.records.size(); ++k) {
                RecordChange rc;
                rc.kind = RecordChange::Kind::Added;
                rc.label = LabelAt(ob.records, k);
                rc.after = ob.records[k].text;
                c.records.push_back(std::move(rc));
                c.layout.push_back({-1, ob.records[k].text});
            }
            changed[j] = 1;
            continue;
        }
        const std::size_t i = (std::size_t)mt.bToA[j];
        const ModelObject& oa = A.objects[i];
        c.kind = ObjectChange::Kind::Modified;
        if (oa.name != ob.name) { c.renamed = true; c.oldName = oa.name; }
        if (parentKeyOf(A, kA, i) != c.parentKey) {
            c.reparented = true;
            c.oldParentPath = parentPathOf(A, i);
            c.newParentPath = parentPathOf(B, j);
        }
        if (oa.guid != ob.guid) c.guidChanged = true;
        if (oa.headerTail != ob.headerTail) c.headerTailChanged = true;
        DiffRecords(oa.records, ob.records, c.records, c.layout, c.recordsReordered);
        changed[j] = c.renamed || c.reparented || c.guidChanged || c.headerTailChanged || c.recordsReordered || !c.records.empty();
    }

    // Sibling order and file order.
    {
        std::unordered_map<std::string, std::size_t> bIndexByKey;
        for (std::size_t j = 0; j < nb; ++j) bIndexByKey[kB[j]] = j;
        auto lists = [](const Model& m, const std::vector<std::string>& k) {
            std::map<std::string, std::vector<std::string>> out;
            for (int root : m.roots) out[""].push_back(k[(std::size_t)root]);
            for (std::size_t i = 0; i < m.objects.size(); ++i)
                for (int c : m.children[i]) out[k[i]].push_back(k[(std::size_t)c]);
            return out;
        };
        const auto la = lists(A, kA), lb = lists(B, kB);
        for (const auto& kv : lb) {
            auto it = la.find(kv.first);
            if (it == la.end()) continue;
            const std::vector<char> stable = StableElements(it->second, kv.second);
            std::unordered_map<std::string, int> oldPos;
            for (std::size_t q = 0; q < it->second.size(); ++q) oldPos[it->second[q]] = (int)q;
            for (std::size_t q = 0; q < kv.second.size(); ++q) {
                if (stable[q]) continue;
                auto op = oldPos.find(kv.second[q]);
                if (op == oldPos.end()) continue;               // new in this family: reparented or added
                const std::size_t j = bIndexByKey[kv.second[q]];
                if (chB[j].kind != ObjectChange::Kind::Modified) continue;
                chB[j].reordered = true;
                chB[j].oldIndex = op->second;
                chB[j].newIndex = (int)q;
                changed[j] = 1;
            }
        }
        std::vector<std::string> orderA(kA.begin(), kA.end());
        const std::vector<std::string> orderB(kB.begin(), kB.end());
        const std::vector<char> stable = StableElements(orderA, orderB);
        for (std::size_t j = 0; j < nb; ++j) {
            if (stable[j] || mt.bToA[j] < 0) continue;
            chB[j].movedInFile = true;
            changed[j] = 1;
        }
        r.order = orderB;
        for (const auto& kv : lb)
            if (!kv.first.empty() && kv.second.size() >= 2) r.childOrder[kv.first] = kv.second;
    }

    for (std::size_t j = 0; j < nb; ++j) {
        if (!changed[j]) continue;
        if (chB[j].kind == ObjectChange::Kind::Added) ++r.added;
        else { ++r.modified; if (chB[j].renamed) ++r.renamed; if (chB[j].reparented) ++r.reparented; if (chB[j].reordered) ++r.reordered; }
        r.objects.push_back(std::move(chB[j]));
    }
    for (std::size_t i = 0; i < na; ++i) {
        if (mt.aToB[i] >= 0) continue;
        ObjectChange c;
        c.kind = ObjectChange::Kind::Removed;
        c.key = kA[i];
        c.guid = A.objects[i].guid;
        c.name = A.objects[i].name;
        c.path = A.objects[i].path;
        c.oldParentPath = parentPathOf(A, i);
        for (std::size_t k = 0; k < A.objects[i].records.size(); ++k) {
            RecordChange rc;
            rc.kind = RecordChange::Kind::Removed;
            rc.label = LabelAt(A.objects[i].records, k);
            rc.before = A.objects[i].records[k].text;
            c.records.push_back(std::move(rc));
        }
        ++r.removed;
        r.objects.push_back(std::move(c));
    }
    return r;
}

// ---- Apply ----------------------------------------------------------------------

bool SceneDiff::Apply(const std::string& baseText, const Result& diff, std::string* out, std::string* error) {
    auto fail = [&](const std::string& what) { if (error) *error = what; return false; };
    if (!diff.ok) return fail("the diff is not valid: " + diff.error);
    Model A;
    std::string err;
    if (!SceneText::BuildModel(SceneText::Canonicalize(baseText), A, &err)) return fail("base: " + err);
    const std::size_t na = A.objects.size();
    std::vector<std::string> kA(na);
    std::unordered_map<std::string, std::size_t> posA;
    for (std::size_t i = 0; i < na; ++i) { kA[i] = ObjectKey(A.objects[i]); posA[kA[i]] = i; }

    std::unordered_map<std::string, const ObjectChange*> chg;
    std::size_t removedCount = 0;
    for (const auto& c : diff.objects) {
        chg[c.key] = &c;
        if (c.kind == ObjectChange::Kind::Removed) {
            if (!posA.count(c.key)) return fail("the diff removes an object the base does not have: " + c.key);
            ++removedCount;
        }
    }
    std::size_t kept = 0;
    for (const auto& k : diff.order) {
        auto it = chg.find(k);
        if (it == chg.end() || it->second->kind != ObjectChange::Kind::Added) ++kept;
    }
    if (kept + removedCount != na) return fail("the diff does not fit this base (object count differs)");

    Model N;
    N.version = diff.otherVersion;
    for (const auto& slot : diff.globalLayout) {
        if (slot.baseIndex >= 0) {
            if ((std::size_t)slot.baseIndex >= A.globals.size()) return fail("file-level record out of range");
            N.globals.push_back(A.globals[(std::size_t)slot.baseIndex]);
        } else N.globals.push_back(MakeRecord(slot.text));
    }
    std::vector<std::string> parentKeys;
    std::unordered_map<std::string, int> posOf;
    for (const auto& k : diff.order) {
        ModelObject mo;
        std::string parentKey;
        auto it = chg.find(k);
        if (it != chg.end() && it->second->kind == ObjectChange::Kind::Added) {
            const ObjectChange& c = *it->second;
            mo.name = c.name;
            mo.guid = c.guid;
            mo.headerTail = c.newHeaderTail;
            for (const auto& s : c.layout) mo.records.push_back(MakeRecord(s.text));
            parentKey = c.parentKey;
        } else {
            auto ai = posA.find(k);
            if (ai == posA.end()) return fail("the diff names an object the base does not have: " + k);
            mo = A.objects[ai->second];
            const int p = mo.parent;
            parentKey = p < 0 ? std::string() : kA[(std::size_t)p];
            if (it != chg.end()) {
                const ObjectChange& c = *it->second;
                if (c.renamed) mo.name = c.name;
                if (c.reparented) parentKey = c.parentKey;
                if (c.guidChanged) mo.guid = c.newGuid;
                if (c.headerTailChanged) mo.headerTail = c.newHeaderTail;
                if (!c.records.empty() || c.recordsReordered) {
                    std::vector<Record> recs;
                    for (const auto& s : c.layout) {
                        if (s.baseIndex >= 0) {
                            if ((std::size_t)s.baseIndex >= A.objects[ai->second].records.size()) return fail("record out of range in " + k);
                            recs.push_back(A.objects[ai->second].records[(std::size_t)s.baseIndex]);
                        } else recs.push_back(MakeRecord(s.text));
                    }
                    mo.records = std::move(recs);
                }
            }
        }
        posOf[k] = (int)N.objects.size();
        N.objects.push_back(std::move(mo));
        parentKeys.push_back(parentKey);
    }
    for (std::size_t i = 0; i < N.objects.size(); ++i) {
        if (parentKeys[i].empty()) { N.objects[i].parent = -1; continue; }
        auto p = posOf.find(parentKeys[i]);
        if (p == posOf.end()) return fail("parent not found: " + parentKeys[i]);
        N.objects[i].parent = p->second;
    }
    SceneText::SetFileChildOrder(N);
    for (const auto& kv : diff.childOrder) {
        auto p = posOf.find(kv.first);
        if (p == posOf.end()) return fail("sibling order names an unknown parent");
        std::vector<int> list;
        for (const auto& ck : kv.second) {
            auto c = posOf.find(ck);
            if (c == posOf.end() || N.objects[(std::size_t)c->second].parent != p->second) return fail("sibling order does not match the hierarchy");
            list.push_back(c->second);
        }
        std::vector<int> a = list, b = N.children[(std::size_t)p->second];
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        if (a != b) return fail("sibling order does not match the hierarchy");
        N.children[(std::size_t)p->second] = list;
    }
    const std::string text = SceneText::Emit(N);
    const SceneText::Validation v = SceneText::Validate(text);
    if (!v.ok) return fail("the patched file is not valid: " + (v.problems.empty() ? std::string("?") : v.problems[0]));
    if (out) *out = text;
    return true;
}

// ---- readable and JSON forms --------------------------------------------------

namespace {

std::string ValueText(const std::string& v, bool present, std::size_t maxChars) {
    return present ? ShortValue(v, maxChars) : "(none)";
}

std::string Where(const std::string& path) { return path.empty() ? "(scene root)" : path; }

void AppendRecordChange(std::string& out, const SceneDiff::RecordChange& c, std::size_t maxChars, const char* indent) {
    using K = SceneDiff::RecordChange::Kind;
    if (c.kind == K::Added) { out += indent; out += "+ " + c.label + "  " + ShortValue(c.after, maxChars) + "\n"; return; }
    if (c.kind == K::Removed) { out += indent; out += "- " + c.label + "  " + ShortValue(c.before, maxChars) + "\n"; return; }
    if (c.fields.empty()) { out += indent; out += "~ " + c.label + "  (formatting only)\n"; return; }
    for (const auto& f : c.fields) {
        out += indent;
        out += c.label;
        if (!f.name.empty()) out += (f.name[0] == '[' ? "" : ".") + f.name;
        out += " " + ValueText(f.before, f.hadBefore, maxChars) + " -> " + ValueText(f.after, f.hasAfter, maxChars) + "\n";
    }
}

std::string ShortKey(const SceneDiff::ObjectChange& c) {
    if (c.guid.empty()) return "";
    return " [" + (c.guid.size() > 8 ? c.guid.substr(0, 8) : c.guid) + "]";
}

} // namespace

std::string SceneDiff::Result::ToText(std::size_t maxValueChars) const {
    if (!ok) return "scene diff failed: " + error + "\n";
    std::string out;
    if (Empty()) return "scene diff: no differences\n";
    out += "scene diff: " + std::to_string(added) + " added, " + std::to_string(removed) + " removed, " + std::to_string(modified) + " modified";
    if (renamed || reparented || reordered) {
        out += " (";
        bool first = true;
        auto part = [&](int n, const char* w) { if (!n) return; if (!first) out += ", "; first = false; out += std::to_string(n) + " " + w; };
        part(renamed, "renamed"); part(reparented, "reparented"); part(reordered, "reordered");
        out += ")";
    }
    out += "\n";
    if (baseVersion != otherVersion)
        out += "format version: " + std::to_string(baseVersion) + " -> " + std::to_string(otherVersion) + "\n";
    if (!globals.empty()) {
        out += "scene settings:\n";
        for (const auto& g : globals) AppendRecordChange(out, g, maxValueChars, "  ");
    }
    for (const auto& c : objects) {
        const std::string pathText = c.path.empty() ? c.name : c.path;
        if (c.kind == ObjectChange::Kind::Added) {
            out += "+ " + pathText + ShortKey(c) + "\n";
            if (!c.records.empty()) {
                out += "    records:";
                for (const auto& rc : c.records) out += " " + rc.label;
                out += "\n";
            }
        } else if (c.kind == ObjectChange::Kind::Removed) {
            out += "- " + pathText + ShortKey(c) + "\n";
        } else {
            out += "~ " + pathText + ShortKey(c) + "\n";
            if (c.renamed) out += "    renamed \"" + c.oldName + "\" -> \"" + c.name + "\"\n";
            if (c.reparented) out += "    reparented " + Where(c.oldParentPath) + " -> " + Where(c.newParentPath) + "\n";
            if (c.reordered) out += "    reordered among siblings " + std::to_string(c.oldIndex) + " -> " + std::to_string(c.newIndex) + "\n";
            if (c.movedInFile) out += "    moved in file order\n";
            if (c.recordsReordered) out += "    records reordered\n";
            if (c.guidChanged) out += "    id changed to " + (c.newGuid.empty() ? std::string("(none)") : c.newGuid) + "\n";
            for (const auto& rc : c.records) AppendRecordChange(out, rc, maxValueChars, "    ");
        }
    }
    return out;
}

namespace {

void JsonRecord(std::string& o, const SceneDiff::RecordChange& c) {
    using K = SceneDiff::RecordChange::Kind;
    o += "{\"kind\":";
    o += c.kind == K::Added ? "\"added\"" : c.kind == K::Removed ? "\"removed\"" : "\"modified\"";
    o += ",\"label\":" + JsonString(c.label);
    if (c.kind != K::Added) o += ",\"before\":" + JsonString(c.before);
    if (c.kind != K::Removed) o += ",\"after\":" + JsonString(c.after);
    if (!c.fields.empty()) {
        o += ",\"fields\":[";
        for (std::size_t i = 0; i < c.fields.size(); ++i) {
            const auto& f = c.fields[i];
            if (i) o += ",";
            o += "{\"field\":" + JsonString(f.name);
            if (f.hadBefore) o += ",\"before\":" + JsonString(f.before);
            if (f.hasAfter) o += ",\"after\":" + JsonString(f.after);
            o += "}";
        }
        o += "]";
    }
    o += "}";
}

} // namespace

std::string SceneDiff::Result::ToJson() const {
    std::string o;
    if (!ok) return "{\"ok\":false,\"error\":" + JsonString(error) + "}\n";
    o += "{\"ok\":true,\"baseVersion\":" + std::to_string(baseVersion) + ",\"otherVersion\":" + std::to_string(otherVersion);
    o += ",\"summary\":{\"added\":" + std::to_string(added) + ",\"removed\":" + std::to_string(removed) + ",\"modified\":" +
         std::to_string(modified) + ",\"renamed\":" + std::to_string(renamed) + ",\"reparented\":" + std::to_string(reparented) +
         ",\"reordered\":" + std::to_string(reordered) + "}";
    o += ",\"globals\":[";
    for (std::size_t i = 0; i < globals.size(); ++i) { if (i) o += ","; JsonRecord(o, globals[i]); }
    o += "],\"objects\":[";
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const auto& c = objects[i];
        if (i) o += ",";
        o += "{\"kind\":";
        o += c.kind == ObjectChange::Kind::Added ? "\"added\"" : c.kind == ObjectChange::Kind::Removed ? "\"removed\"" : "\"modified\"";
        o += ",\"key\":" + JsonString(c.key) + ",\"guid\":" + JsonString(c.guid) + ",\"name\":" + JsonString(c.name) + ",\"path\":" + JsonString(c.path);
        if (c.renamed) o += ",\"renamed\":{\"from\":" + JsonString(c.oldName) + ",\"to\":" + JsonString(c.name) + "}";
        if (c.reparented) o += ",\"reparented\":{\"from\":" + JsonString(c.oldParentPath) + ",\"to\":" + JsonString(c.newParentPath) + "}";
        if (c.reordered) o += ",\"reordered\":{\"from\":" + std::to_string(c.oldIndex) + ",\"to\":" + std::to_string(c.newIndex) + "}";
        if (c.movedInFile) o += ",\"movedInFile\":true";
        if (c.recordsReordered) o += ",\"recordsReordered\":true";
        if (c.guidChanged) o += ",\"guidChanged\":" + JsonString(c.newGuid);
        o += ",\"records\":[";
        for (std::size_t k = 0; k < c.records.size(); ++k) { if (k) o += ","; JsonRecord(o, c.records[k]); }
        o += "]}";
    }
    o += "]}\n";
    return o;
}

} // namespace okay
