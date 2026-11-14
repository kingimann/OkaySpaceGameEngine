#include "SceneTextDetail.hpp"

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

namespace okay {
namespace scenetext {

std::string ObjectKey(const ModelObject& o) { return o.guid.empty() ? "p:" + o.path : "g:" + o.guid; }

Matching MatchObjects(const Model& a, const Model& b, bool detectRenames) {
    Matching m;
    m.aToB.assign(a.objects.size(), -1);
    m.bToA.assign(b.objects.size(), -1);
    auto link = [&](std::size_t i, std::size_t j) { m.aToB[i] = (int)j; m.bToA[j] = (int)i; };

    std::unordered_map<std::string, std::size_t> byGuid;
    for (std::size_t j = 0; j < b.objects.size(); ++j)
        if (!b.objects[j].guid.empty()) byGuid[b.objects[j].guid] = j;
    for (std::size_t i = 0; i < a.objects.size(); ++i) {
        if (a.objects[i].guid.empty()) continue;
        auto it = byGuid.find(a.objects[i].guid);
        if (it != byGuid.end() && m.bToA[it->second] < 0) link(i, it->second);
    }

    // Files from before object ids, or one side saved by a build that adds them: a name
    // path pairs an object with one that has no id on one of the two sides.
    std::unordered_map<std::string, std::size_t> byPath;
    for (std::size_t j = 0; j < b.objects.size(); ++j) if (m.bToA[j] < 0) byPath[b.objects[j].path] = j;
    for (std::size_t i = 0; i < a.objects.size(); ++i) {
        if (m.aToB[i] >= 0) continue;
        auto it = byPath.find(a.objects[i].path);
        if (it == byPath.end() || m.bToA[it->second] >= 0) continue;
        if (a.objects[i].guid.empty() || b.objects[it->second].guid.empty()) link(i, it->second);
    }

    if (detectRenames) {
        for (int pass = 0; pass < 4; ++pass) {
            std::unordered_map<std::string, std::vector<std::size_t>> asig, bsig;
            auto content = [](const ModelObject& o) {
                std::string s;
                for (const auto& r : o.records) { s += r.text; s += '\n'; }
                return s;
            };
            for (std::size_t i = 0; i < a.objects.size(); ++i) {
                if (m.aToB[i] >= 0 || !a.objects[i].guid.empty()) continue;
                const int p = a.objects[i].parent;
                if (p >= 0 && m.aToB[(std::size_t)p] < 0) continue;
                asig[std::to_string(p) + "\x01" + content(a.objects[i])].push_back(i);
            }
            for (std::size_t j = 0; j < b.objects.size(); ++j) {
                if (m.bToA[j] >= 0 || !b.objects[j].guid.empty()) continue;
                const int p = b.objects[j].parent;
                const int pa = p >= 0 ? m.bToA[(std::size_t)p] : -1;
                if (p >= 0 && pa < 0) continue;
                bsig[std::to_string(pa) + "\x01" + content(b.objects[j])].push_back(j);
            }
            bool progress = false;
            for (const auto& kv : asig) {
                auto it = bsig.find(kv.first);
                if (kv.second.size() != 1 || it == bsig.end() || it->second.size() != 1) continue;
                const std::size_t i = kv.second[0], j = it->second[0];
                if (a.objects[i].name == b.objects[j].name) continue;
                link(i, j);
                progress = true;
            }
            if (!progress) break;
        }
    }
    return m;
}

std::vector<char> StableElements(const std::vector<std::string>& base, const std::vector<std::string>& other) {
    std::unordered_map<std::string, int> posInBase;
    posInBase.reserve(base.size() * 2);
    for (std::size_t i = 0; i < base.size(); ++i) posInBase.emplace(base[i], (int)i);
    std::vector<char> keep(other.size(), 0);
    std::vector<int> idx, val;                    // elements of `other` that are also in `base`
    for (std::size_t j = 0; j < other.size(); ++j) {
        auto it = posInBase.find(other[j]);
        if (it != posInBase.end()) { idx.push_back((int)j); val.push_back(it->second); }
    }
    // Longest strictly increasing subsequence of `val` (patience sorting).
    std::vector<int> tails, tailAt, prev(val.size(), -1);
    for (std::size_t k = 0; k < val.size(); ++k) {
        const auto pos = (std::size_t)(std::lower_bound(tails.begin(), tails.end(), val[k]) - tails.begin());
        if (pos == tails.size()) { tails.push_back(val[k]); tailAt.push_back((int)k); }
        else { tails[pos] = val[k]; tailAt[pos] = (int)k; }
        prev[k] = pos ? tailAt[pos - 1] : -1;
    }
    for (int k = tails.empty() ? -1 : tailAt.back(); k >= 0; k = prev[(std::size_t)k]) keep[(std::size_t)idx[(std::size_t)k]] = 1;
    return keep;
}

std::string RecordIdentity(const Record& r) {
    if (r.key == "compoff") return r.text;
    if (r.key == "override") {
        const auto toks = SceneText::Tokenize(r.text);
        if (toks.size() >= 3) {
            const std::string inner = SceneText::Unquote(toks[2].text);
            return "override:" + toks[1].text + ":" + SceneText::KeyOf(inner);
        }
    }
    return r.key;
}

namespace {

// Pair the records of one identity: identical text first (an LCS, so order is kept),
// then the leftovers between two anchors one to one in order (those are the edited ones).
void PairGroup(const std::vector<Record>& A, const std::vector<Record>& B,
               const std::vector<int>& va, const std::vector<int>& vb,
               std::vector<int>& aOfB, std::vector<char>& aUsed) {
    const int na = (int)va.size(), nb = (int)vb.size();
    std::vector<std::pair<int, int>> anchors;
    if ((long long)na * nb <= 160000) {
        std::vector<int> dp((std::size_t)(na + 1) * (std::size_t)(nb + 1), 0);
        auto at = [&](int i, int j) -> int& { return dp[(std::size_t)i * (std::size_t)(nb + 1) + (std::size_t)j]; };
        for (int i = na - 1; i >= 0; --i)
            for (int j = nb - 1; j >= 0; --j)
                at(i, j) = A[(std::size_t)va[(std::size_t)i]].text == B[(std::size_t)vb[(std::size_t)j]].text
                               ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
        for (int i = 0, j = 0; i < na && j < nb;) {
            if (A[(std::size_t)va[(std::size_t)i]].text == B[(std::size_t)vb[(std::size_t)j]].text) { anchors.push_back({i, j}); ++i; ++j; }
            else if (at(i + 1, j) >= at(i, j + 1)) ++i;
            else ++j;
        }
    } else {
        // Big groups (a spline with thousands of points): greedy in order, with a lookup table.
        std::unordered_map<std::string, std::vector<int>> at;
        for (int j = 0; j < nb; ++j) at[B[(std::size_t)vb[(std::size_t)j]].text].push_back(j);
        int j0 = 0;
        for (int i = 0; i < na && j0 < nb; ++i) {
            auto f = at.find(A[(std::size_t)va[(std::size_t)i]].text);
            if (f == at.end()) continue;
            auto it = std::lower_bound(f->second.begin(), f->second.end(), j0);
            if (it == f->second.end()) continue;
            anchors.push_back({i, *it});
            j0 = *it + 1;
        }
    }
    for (const auto& an : anchors) {   // identical records pair with each other
        aOfB[(std::size_t)vb[(std::size_t)an.second]] = va[(std::size_t)an.first];
        aUsed[(std::size_t)va[(std::size_t)an.first]] = 1;
    }
    anchors.insert(anchors.begin(), {-1, -1});
    anchors.push_back({na, nb});
    for (std::size_t k = 0; k + 1 < anchors.size(); ++k) {   // the records between two anchors: edited ones, paired in order
        const int ia0 = anchors[k].first, ib0 = anchors[k].second, ia1 = anchors[k + 1].first, ib1 = anchors[k + 1].second;
        for (int i = ia0 + 1, j = ib0 + 1; i < ia1 && j < ib1; ++i, ++j) {
            aOfB[(std::size_t)vb[(std::size_t)j]] = va[(std::size_t)i];
            aUsed[(std::size_t)va[(std::size_t)i]] = 1;
        }
    }
}

} // namespace

std::vector<RecordPair> AlignRecords(const std::vector<Record>& A, const std::vector<Record>& B) {
    std::unordered_map<std::string, std::vector<int>> ga, gb;
    std::vector<std::string> bIdents;
    for (std::size_t i = 0; i < A.size(); ++i) ga[RecordIdentity(A[i])].push_back((int)i);
    for (std::size_t j = 0; j < B.size(); ++j) {
        auto& v = gb[RecordIdentity(B[j])];
        if (v.empty()) bIdents.push_back(RecordIdentity(B[j]));
        v.push_back((int)j);
    }
    std::vector<int> aOfB(B.size(), -1);
    std::vector<char> aUsed(A.size(), 0);
    for (const auto& id : bIdents) {
        auto it = ga.find(id);
        if (it == ga.end()) continue;
        PairGroup(A, B, it->second, gb[id], aOfB, aUsed);
    }
    std::vector<RecordPair> out;
    out.reserve(A.size() + B.size());
    for (std::size_t j = 0; j < B.size(); ++j) out.push_back({aOfB[j], (int)j});
    for (std::size_t i = 0; i < A.size(); ++i) if (!aUsed[i]) out.push_back({(int)i, -1});
    return out;
}

// ---- field names and labels ---------------------------------------------------

namespace {
struct Names { const char* key; const char* label; std::vector<const char*> fields; };
const std::vector<Names>& NameTable() {
    static const std::vector<Names> t = {
        {"transform", "Transform", {"px", "py", "pz", "qx", "qy", "qz", "qw", "sx", "sy", "sz"}},
        {"light", "Light", {"color.r", "color.g", "color.b", "color.a", "ambient", "intensity", "type", "range", "spotAngle",
                            "spotSoftness", "useTemperature", "temperature", "ambientColor.r", "ambientColor.g", "ambientColor.b", "falloff"}},
        {"sprite", "Sprite", {"glyph", "color.r", "color.g", "color.b", "color.a", "size.x", "size.y", "texture", "uvMin.x", "uvMin.y",
                              "uvMax.x", "uvMax.y", "sortOrder", "flipX", "flipY", "sortingLayer", "texFilter"}},
        {"camera", "Camera", {"projection", "orthographicSize", "fieldOfView", "background.r", "background.g", "background.b", "background.a",
                              "main", "clearFlags", "depth", "nearClip", "farClip", "fovAxis", "rect.x", "rect.y", "rect.w", "rect.h",
                              "cullingMask", "physical", "focalLength", "sensorHeight", "zoom", "dutch", "lensShift.x", "lensShift.y",
                              "pixelPerfect", "pixelsPerUnit", "shakeAmount", "shakeAngle", "shakeFrequency", "shakeRecovery"}},
        {"rigidbody2d", "Rigidbody2D", {"bodyType", "gravityScale", "mass", "drag", "bounciness", "friction", "angularDrag",
                                        "freezeRotation", "allowSleep", "continuous"}},
        {"rigidbody3d", "Rigidbody3D", {"bodyType", "gravityScale", "mass", "drag", "bounciness", "freezeX", "freezeY", "freezeZ",
                                        "maxFallSpeed", "friction", "angularDrag", "freezeRotation"}},
        {"boxcollider2d", "BoxCollider2D", {"size.x", "size.y", "offset.x", "offset.y", "isTrigger", "layer", "autoFit"}},
        {"circlecollider2d", "CircleCollider2D", {"radius", "offset.x", "offset.y", "isTrigger", "layer", "autoFit"}},
        {"boxcollider3d", "BoxCollider3D", {"size.x", "size.y", "size.z", "offset.x", "offset.y", "offset.z", "isTrigger", "layer", "autoFit"}},
        {"spherecollider3d", "SphereCollider3D", {"radius", "offset.x", "offset.y", "offset.z"}},
        {"spinner", "Spinner", {"x", "y", "z", "space"}},
        {"lifetime", "Lifetime", {"seconds", "elapsed"}},
        {"mover", "Mover", {"x", "y", "z", "space"}},
        {"audio", "Audio", {"clip", "volume", "loop", "playOnAwake", "spatial", "minDistance", "maxDistance", "pitch", "pan",
                            "group", "rolloff", "lowPass", "stream", "doppler", "dopplerScale", "occlude", "occlusionVolume",
                            "occlusionLowPass", "distanceLowPass", "distanceLowPassHz"}},
        {"text", "Text", {"text", "color.r", "color.g", "color.b", "color.a", "pixelSize", "screenSpace", "screenPos.x", "screenPos.y",
                          "anchor", "shadow", "shadowColor.r", "shadowColor.g", "shadowColor.b", "shadowColor.a", "shadowOffset.x",
                          "shadowOffset.y", "align", "outline", "outlineColor.r", "outlineColor.g", "outlineColor.b", "outlineColor.a"}},
        {"script", "Script", {"language", "source", "path", "fieldCount"}},
        {"gravity", "gravity", {"x", "y"}},
        {"gravity3", "gravity3", {"x", "y", "z"}},
        {"fog", "fog", {"on", "color.r", "color.g", "color.b", "start", "end"}},
        {"active", "Active", {}}, {"tag", "Tag", {}}, {"static", "Static", {}}, {"locked", "Locked", {}},
        {"layer", "Layer", {}}, {"uiorder", "UiOrder", {}}, {"prefab", "Prefab", {}}, {"srcscene", "SrcScene", {}},
        {"name", "name", {}}, {"uifont", "uifont", {}},
    };
    return t;
}
const Names* FindNames(const std::string& key) {
    for (const auto& n : NameTable()) if (key == n.key) return &n;
    return nullptr;
}
bool IsIdent(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) return false;
    return true;
}
} // namespace

std::string RecordLabel(const std::string& key) {
    const Names* n = FindNames(key);
    return n ? n->label : key;
}

std::string LabelAt(const std::vector<Record>& recs, std::size_t idx) {
    int occ = 0, total = 0;
    for (std::size_t i = 0; i < recs.size(); ++i)
        if (recs[i].key == recs[idx].key) { if (i < idx) ++occ; ++total; }
    std::string l = RecordLabel(recs[idx].key);
    if (recs[idx].key == "compoff" || recs[idx].key == "override") l = recs[idx].key;
    return total > 1 ? l + "#" + std::to_string(occ) : l;
}

std::string FieldName(const std::string& key, std::size_t i) {
    const Names* n = FindNames(key);
    if (n && i < n->fields.size()) return n->fields[i];
    return "[" + std::to_string(i) + "]";
}

bool IsKeyValueRecord(const std::vector<SceneText::Token>& toks) {
    if (toks.size() < 2) return false;
    std::unordered_set<std::string> seen;
    for (std::size_t i = 1; i < toks.size(); ++i) {
        const std::string& t = toks[i].text;
        const std::size_t eq = t.find('=');
        if (eq == std::string::npos || !IsIdent(t.substr(0, eq))) return false;
        if (!seen.insert(t.substr(0, eq)).second) return false;
    }
    return true;
}

std::vector<FieldDelta> DiffFields(const Record& a, const Record& b) {
    std::vector<FieldDelta> out;
    const auto ta = SceneText::Tokenize(a.text), tb = SceneText::Tokenize(b.text);
    if (ta.empty() || tb.empty()) return out;
    if (IsKeyValueRecord(ta) && IsKeyValueRecord(tb)) {
        std::unordered_map<std::string, std::string> vb, va;
        for (std::size_t i = 1; i < ta.size(); ++i) { const auto eq = ta[i].text.find('='); va[ta[i].text.substr(0, eq)] = ta[i].text.substr(eq + 1); }
        for (std::size_t i = 1; i < tb.size(); ++i) { const auto eq = tb[i].text.find('='); vb[tb[i].text.substr(0, eq)] = tb[i].text.substr(eq + 1); }
        for (std::size_t i = 1; i < ta.size(); ++i) {
            const std::string k = ta[i].text.substr(0, ta[i].text.find('='));
            auto it = vb.find(k);
            if (it == vb.end()) out.push_back({k, va[k], "", true, false});
            else if (it->second != va[k]) out.push_back({k, va[k], it->second, true, true});
        }
        for (std::size_t i = 1; i < tb.size(); ++i) {
            const std::string k = tb[i].text.substr(0, tb[i].text.find('='));
            if (!va.count(k)) out.push_back({k, "", vb[k], false, true});
        }
        return out;
    }
    const std::size_t n = std::max(ta.size(), tb.size());
    const bool single = ta.size() == 2 && tb.size() == 2;
    for (std::size_t i = 1; i < n; ++i) {
        const bool hb = i < ta.size(), ha = i < tb.size();
        const std::string before = hb ? ta[i].text : std::string(), after = ha ? tb[i].text : std::string();
        if (hb && ha && before == after) continue;
        out.push_back({single ? std::string() : FieldName(a.key, i - 1), before, after, hb, ha});
    }
    return out;
}

std::string ShortValue(const std::string& v, std::size_t maxChars) {
    std::string out;
    for (char c : v) {
        if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    if (out.size() > maxChars && maxChars > 3) out = out.substr(0, maxChars - 3) + "...";
    return out;
}

std::string JsonString(const std::string& s) {
    std::string out = "\"";
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else if (c < 0x80) out += (char)c;
                else {
                    // Pass a valid UTF-8 sequence through; a stray byte becomes \u00XX.
                    int len = (c >= 0xF0 && c < 0xF8) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC2 && c < 0xE0) ? 2 : 0;
                    bool ok = len > 0 && c < 0xF8 && i + (std::size_t)len <= s.size();
                    for (int k = 1; ok && k < len; ++k) ok = ((unsigned char)s[i + (std::size_t)k] & 0xC0) == 0x80;
                    if (ok) { out.append(s, i, (std::size_t)len); i += (std::size_t)len - 1; }
                    else { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                }
        }
    }
    out += '"';
    return out;
}

} // namespace scenetext
} // namespace okay
