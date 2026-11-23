// Private to the scene text reader and writer (SceneSerializer*.cpp). The small helpers every record reader and writer
// shares: quoting, the key=value line reader and writer, and the list and curve forms. They used to sit in an anonymous
// namespace at the top of SceneSerializer.cpp (docs/engine/modularization_plan.md, section 3.1, S3).
#pragma once
#include "okay/Components/Consumables.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/StanceControl.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Animation/JointLimit.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Inventory/Recipe.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/PhysicsMaterial.hpp"
#include "okay/Scene/GameObject.hpp"
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {
namespace sceneser {

struct KvOut;
struct KvIn;

void UseRoundTripFloats(std::ostream& o);
std::string Quote(const std::string& s);
std::string ReadQuoted(std::istream& in);
std::string KeyToken(char key);
char KeyFromToken(const std::string& t);
std::string ItemId(const std::string& name);
void WriteRecipeExtras(std::ostream& out, const std::vector<Recipe>& rs);
void ReadRecipeExtras(std::istream& in, std::vector<Recipe>& rs);
std::string ReadRecordLine(std::istream& in);
void WriteLimitKv(KvOut& k, const std::string& p, const IKJointLimit& l);
void ReadLimitKv(const KvIn& k, const std::string& p, IKJointLimit& l);
std::vector<Collider3D*> LegacyColliders3D(const GameObject& go);
std::vector<Collider2D*> LegacyColliders2D(const GameObject& go);
void WriteStanceControl(std::ostream& out, const char* rec, const StanceControl& sc);
void ReadStanceControl(std::istream& in, StanceControl* sc);
std::string PointList(const std::vector<Vec2>& v);
std::string PointList(const std::vector<Vec3>& v);
std::vector<float> ReadNumberList(const KvIn& k, const char* key);
void WriteCurveKv(KvOut& k, const std::string& p, const ParticleCurve& c);
void ReadCurveKv(const KvIn& k, const std::string& p, ParticleCurve& c);
void WriteGradientKv(KvOut& k, const std::string& p, const ParticleGradient& g);
void ReadGradientKv(const KvIn& k, const std::string& p, ParticleGradient& g);
void ReadStatTail(std::istream& in, StatComponent* c);
void ReadAnchor(std::istream& in, UIAnchor& anchor);

// True when the next token on the stream is a number the reader accepts. The writer
// prints floats through std::to_chars, which emits '-', '+', '.' and digits, so
// optional-field gates have to accept all of them: a gate that peeked for digits
// only made a negative value (an audio pitch of -1) look like absent fields, the
// rest of the line was dropped, and the next save wrote the component wrong.
// Compares against eof before isdigit, which is undefined for a negative int.
inline bool LooksNumeric(std::istream& in) {
    in >> std::ws;
    const int c = in.peek();
    if (c == std::char_traits<char>::eof()) return false;
    return c == '-' || c == '+' || c == '.' || std::isdigit(c) != 0;
}

// How much of a newer build's text is kept per object and per scene (a hostile file must not make a scene huge).
constexpr std::size_t kMaxUnknownRecords = 256, kMaxUnknownRecordSize = 65536;

// The last of a list of components (nullptr when empty): a follow-up record such as `particlesfx` belongs to the
// component the record before it made, which is the newest one when an object holds several of a type.
template <class T>
T* LastOf(const std::vector<T*>& v) { return v.empty() ? nullptr : v.back(); }

// The component a follow-up record belongs to when the records of one type are not
// grouped with it (the Nth `ikx aim` is the Nth Aim IK). One instance: N is 0, the first.
template <class T>
T* NthOf(GameObject* go, int& n) {
    const std::vector<T*> all = go->GetComponents<T>();
    const int i = n++;
    return (i >= 0 && (std::size_t)i < all.size()) ? all[(std::size_t)i] : nullptr;
}

template <class T>
T* AtComp(const std::vector<T*>& v, int i) {
    return (i >= 0 && i < (int)v.size()) ? v[(std::size_t)i] : nullptr;
}

// Per-instance item data (durability / uid / values), appended after a container's
// other fields as: count, then per entry: index durability uid nvalues "k" "v"...
template <class Get>
void WriteInstanceData(std::ostream& out, std::size_t n, Get data) {
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < n; ++i) if (!data(i).Empty()) idx.push_back(i);
    out << " " << idx.size();
    for (std::size_t i : idx) {
        const ItemInstanceData& d = data(i);
        out << " " << i << " " << d.durability << " " << d.uid << " " << d.values.size();
        for (const auto& kv : d.values) out << " " << Quote(kv.first) << " " << Quote(kv.second);
    }
}
template <class Get>
void ReadInstanceData(std::istream& in, std::size_t n, Get data) {
    in >> std::ws;
    const int pk = in.peek();
    if (!std::isdigit(pk)) return;   // older line: no instance data
    std::size_t count = 0; in >> count;
    for (std::size_t k = 0; k < count && in; ++k) {
        std::size_t i = 0, nv = 0;
        ItemInstanceData d;
        in >> i >> d.durability >> d.uid >> nv;
        for (std::size_t v = 0; v < nv && in; ++v) { std::string key = ReadQuoted(in); d.values.push_back({key, ReadQuoted(in)}); }
        if (in && i < n) data(i) = d;
    }
}

// key=value component lines (used by newer components): order-free, and keys a
// build doesn't know are skipped, so fields can be added without breaking saves.
struct KvOut {
    std::ostream& o;
    template <class T> KvOut& operator()(const char* k, const T& v) { o << ' ' << k << '=' << v; return *this; }
    KvOut& b(const char* k, bool v) { o << ' ' << k << '=' << (v ? 1 : 0); return *this; }
    KvOut& key(const char* k, char v) { o << ' ' << k << '=' << (int)(unsigned char)v; return *this; }
    KvOut& str(const char* k, const std::string& v) { o << ' ' << k << '=' << Quote(v); return *this; }
    KvOut& col(const char* k, const Color& c) {
        o << ' ' << k << "R=" << c.r << ' ' << k << "G=" << c.g << ' ' << k << "B=" << c.b << ' ' << k << "A=" << c.a;
        return *this;
    }
};
struct KvIn {
    std::unordered_map<std::string, std::string> m;
    explicit KvIn(std::istream& in) {
        const std::string line = ReadRecordLine(in);
        std::istringstream ls(line);
        while (true) {
            ls >> std::ws;
            if (!ls || ls.peek() == EOF) break;
            std::string k; char c;
            while (ls.get(c) && c != '=' && !std::isspace((unsigned char)c)) k += c;
            if (c != '=') continue;
            if (ls.peek() == '"') m[k] = ReadQuoted(ls);
            else { std::string v; ls >> v; m[k] = v; }
        }
    }
    const std::string* find(const char* k) const { auto it = m.find(k); return it == m.end() ? nullptr : &it->second; }
    void f(const char* k, float& v) const { if (auto* s = find(k)) v = std::strtof(s->c_str(), nullptr); }
    void i(const char* k, int& v) const { if (auto* s = find(k)) v = std::atoi(s->c_str()); }
    void u(const char* k, std::uint32_t& v) const { if (auto* s = find(k)) v = (std::uint32_t)std::strtoul(s->c_str(), nullptr, 10); }
    void b(const char* k, bool& v) const { if (auto* s = find(k)) v = std::atoi(s->c_str()) != 0; }
    void key(const char* k, char& v) const { if (auto* s = find(k)) v = (char)std::atoi(s->c_str()); }
    void str(const char* k, std::string& v) const { if (auto* s = find(k)) v = *s; }
    void col(const char* k, Color& c) const {
        std::string b = k;
        f((b + "R").c_str(), c.r); f((b + "G").c_str(), c.g); f((b + "B").c_str(), c.b); f((b + "A").c_str(), c.a);
    }
};
// One trigger-filter line when every collider on the object shares it (a file from before
// per-collider filters, applied to all of them on load). When they differ, one line per collider
// that is set, with its index, so each keeps its own tag.
template <class T>
void WriteTriggerFilters(std::ostream& out, const char* rec, const std::vector<T*>& cols) {
    const T* sample = nullptr;
    for (const T* c : cols)
        if (!c->triggerTag.empty() || c->triggerLayers != 0xFFFFFFFFu) { sample = c; break; }
    bool mixed = false;
    if (sample)
        for (const T* c : cols)
            if (c->triggerTag != sample->triggerTag || c->triggerLayers != sample->triggerLayers) { mixed = true; break; }
    if (!mixed) {
        if (sample)
            out << "  " << rec << " " << Quote(sample->triggerTag) << " " << sample->triggerLayers << "\n";
        return;
    }
    for (int i = 0; i < (int)cols.size(); ++i) {
        const T* c = cols[(std::size_t)i];
        if (c->triggerTag.empty() && c->triggerLayers == 0xFFFFFFFFu) continue;
        out << "  " << rec << " " << Quote(c->triggerTag) << " " << c->triggerLayers << " " << i << "\n";
    }
}

// The physics material of a collider (friction, bounciness, how each is combined): one line per collider whose
// material is not the default, with its index. Colliders with the default material write nothing, so older scenes
// and older builds see the same files as before.
template <class T>
void WritePhysicsMaterials(std::ostream& out, const char* rec, const std::vector<T*>& cols) {
    for (int i = 0; i < (int)cols.size(); ++i) {
        const T* c = cols[(std::size_t)i];
        if (c->friction < 0.0f && c->bounciness < 0.0f && c->frictionCombine == CombineMode::Default &&
            c->bounceCombine == CombineMode::Default) continue;
        out << "  " << rec << " " << c->friction << " " << c->bounciness << " " << (int)c->frictionCombine
            << " " << (int)c->bounceCombine << " " << i << "\n";
    }
}
// The contact offset (a skin on the collider): one line per collider that has one, so a scene without any writes nothing.
template <class T>
void WriteContactOffsets(std::ostream& out, const char* rec, const std::vector<T*>& cols) {
    for (int i = 0; i < (int)cols.size(); ++i)
        if (cols[(std::size_t)i]->contactOffset > 0.0f) out << "  " << rec << " " << cols[(std::size_t)i]->contactOffset << " " << i << "\n";
}

} // namespace sceneser
} // namespace okay
