// Helpers shared by the scene text reader and writer (declared in SceneSerializerInternal.hpp). Moved unchanged from
// SceneSerializer.cpp (docs/engine/modularization_plan.md, section 3.1, S1).
#include "SceneSerializerInternal.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include <charconv>
#include <locale>
#include <algorithm>
#include <iterator>

namespace okay {
namespace sceneser {

// Floats in scene text are written with the fewest digits that read back as the
// same value (the default stream precision keeps 6, so 12345.678 came back as
// 12345.7 and every save, undo step and prefab clone rounded again). A stream
// hands floats to the facet as doubles, so a double that is exactly a float is
// printed as that float, which is what every float field wants; anything else
// prints as the shortest double. Streams with their own precision, width or
// fixed / scientific flags keep the standard formatting. Short values print as
// before (0.5 is still 0.5), so old files and text compares are unaffected.
class RoundTripNumPut : public std::num_put<char> {
public:
    explicit RoundTripNumPut(std::size_t refs) : std::num_put<char>(refs) {}
protected:
    iter_type do_put(iter_type out, std::ios_base& str, char fill, double v) const override {
        if ((str.flags() & (std::ios_base::fixed | std::ios_base::scientific)) || str.precision() != 6 ||
            str.width() != 0)
            return std::num_put<char>::do_put(out, str, fill, v);
        char buf[64];
        std::to_chars_result r;
        const float f = static_cast<float>(v);
        if (static_cast<double>(f) == v) r = std::to_chars(buf, buf + sizeof buf, f);
        else                             r = std::to_chars(buf, buf + sizeof buf, v);
        if (r.ec != std::errc()) return std::num_put<char>::do_put(out, str, fill, v);
        for (const char* c = buf; c != r.ptr; ++c) *out++ = *c;
        return out;
    }
};
// TolerantLocale() (reads "nan" / "inf" as numbers) lives in okay/Core/TolerantNumbers.hpp, shared with the .okaymat reader.
void UseRoundTripFloats(std::ostream& o) {
    // The locale is allocated once and never destroyed (a static std::locale object would be torn down at exit,
    // orphaning the facet and tripping leak checkers, and streams used during static destruction need it alive).
    // The facet has refs=0, so the locale owns it.
    static const std::locale* loc = new std::locale(std::locale::classic(), new RoundTripNumPut(0));
    o.imbue(*loc);
}
// Quote a string for the line-based format (escapes quotes/backslashes).
std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

// Read a quoted token from a stream (assumes leading quote already trimmed by >>).
std::string ReadQuoted(std::istream& in) {
    std::string tok;
    in >> std::ws;
    if (in.peek() != '"') { in >> tok; return tok; }
    in.get(); // opening quote
    std::string out;
    char c;
    while (in.get(c)) {
        if (c == '\\') { if (in.get(c)) out += c; }
        else if (c == '"') break;
        else out += c;
    }
    return out;
}

// A key code in the records that name a key as one token (charctrl2d, tdctrl, tdctrl2d, tpsctrl, ctmctrl). Older
// builds wrote the raw character, so a space, a tab or Enter split the record and shifted every later field, and
// '-' (which stood for "no key") read back as no key. Now a printable character other than '-' is still written
// as itself (the form every build reads) and every other code is written by number as `#<0..255>`: space is
// `#32`, '-' is `#45`, Shift is `#16`, F1 is `#128`. No key is `-`. A raw token is always one byte, so the
// two forms cannot be confused; KeyFromToken reads both.
std::string KeyToken(char key) {
    const unsigned char c = static_cast<unsigned char>(key);
    if (c == 0) return "-";
    if (c > 32 && c < 127 && c != '-') return std::string(1, key);
    return "#" + std::to_string(static_cast<int>(c));
}
char KeyFromToken(const std::string& t) {
    if (t.empty() || t == "-") return 0;
    if (t.size() > 1 && t[0] == '#') {
        int v = 0;
        bool ok = true;
        for (std::size_t i = 1; i < t.size() && ok; ++i) {
            if (t[i] < '0' || t[i] > '9') ok = false;
            else { v = v * 10 + (t[i] - '0'); if (v > 255) ok = false; }
        }
        if (ok) return static_cast<char>(static_cast<unsigned char>(v));
    }
    return t[0];   // the raw character older builds wrote
}

// An item name read from a scene, as an ItemDatabase id: legacy names / display
// names map to their definition's id, and unknown names become loose definitions.
std::string ItemId(const std::string& name) { return ItemDatabase::Get().Resolve(name); }

// A recipe list's newer fields (craft time, bench tag, unlock flag, byproducts),
// appended after the older fields: one entry per recipe, in order.
void WriteRecipeExtras(std::ostream& out, const std::vector<Recipe>& rs) {
    for (const auto& r : rs) {
        out << " " << r.craftTime << " " << Quote(r.station) << " " << Quote(r.unlockFlag) << " " << r.byproducts.size();
        for (const auto& b : r.byproducts) out << " " << Quote(b.item) << " " << b.count;
    }
}
void ReadRecipeExtras(std::istream& in, std::vector<Recipe>& rs) {
    for (auto& r : rs) {
        std::size_t nb = 0;
        in >> r.craftTime; r.station = ReadQuoted(in); r.unlockFlag = ReadQuoted(in); in >> nb;
        if (!in) return;
        if (!(r.craftTime >= 0.0f)) r.craftTime = 0.0f;
        for (std::size_t k = 0; k < nb && in; ++k) { Ingredient b; b.item = ItemId(ReadQuoted(in)); in >> b.count; r.byproducts.push_back(b); }
    }
}
// Read the rest of a record's line. A quoted value may itself contain line breaks (a quest description typed
// into a multi-line box is written as it is, Quote escapes only the quote and the backslash), so a line that ends
// inside a quote goes on to the next one until the quote closes. When it never closes (a hand-edited or damaged
// file) the stream is put back and only the first line is taken, as before.
std::string ReadRecordLine(std::istream& in) {
    std::string line;
    // Ask the buffer, not tellg(): tellg() fails once the stream has hit the end.
    const std::streamoff start = in.rdbuf()->pubseekoff(0, std::ios::cur, std::ios::in);
    std::getline(in, line);
    auto endsInQuote = [](const std::string& l) {
        bool open = false;
        for (std::size_t i = 0; i < l.size(); ++i) {
            if (open && l[i] == '\\') ++i;   // an escaped character
            else if (l[i] == '"') open = !open;
        }
        return open;
    };
    if (!endsInQuote(line) || start < 0) return line;
    std::string joined = line;
    std::string next;
    while (std::getline(in, next)) {
        joined += '\n';
        joined += next;
        if (!endsInQuote(joined)) return joined;
    }
    in.clear();
    in.rdbuf()->pubseekpos(start, std::ios::in);   // the quote never closed: only this line belongs to the record
    std::getline(in, line);
    return line;
}
// A joint limit as `<p>T= <p>S= ...` keys (nothing when it's None).
void WriteLimitKv(KvOut& k, const std::string& p, const IKJointLimit& l) {
    if (!l.Active()) return;
    k((p + "T").c_str(), (int)l.type)((p + "S").c_str(), l.swingMax)
     ((p + "H0").c_str(), l.hingeMin)((p + "H1").c_str(), l.hingeMax)
     ((p + "W0").c_str(), l.twistMin)((p + "W1").c_str(), l.twistMax);
    if (!l.autoAxes)
        k((p + "TX").c_str(), l.twistAxis.x)((p + "TY").c_str(), l.twistAxis.y)((p + "TZ").c_str(), l.twistAxis.z)
         ((p + "HX").c_str(), l.hingeAxis.x)((p + "HY").c_str(), l.hingeAxis.y)((p + "HZ").c_str(), l.hingeAxis.z);
}
void ReadLimitKv(const KvIn& k, const std::string& p, IKJointLimit& l) {
    int ty = 0; k.i((p + "T").c_str(), ty);
    if (ty <= 0) return;
    l.type = (IKJointLimit::Type)std::clamp(ty, 0, 2);
    k.f((p + "S").c_str(), l.swingMax);
    k.f((p + "H0").c_str(), l.hingeMin); k.f((p + "H1").c_str(), l.hingeMax);
    k.f((p + "W0").c_str(), l.twistMin); k.f((p + "W1").c_str(), l.twistMax);
    if (k.find((p + "TX").c_str())) {
        l.autoAxes = false;
        k.f((p + "TX").c_str(), l.twistAxis.x); k.f((p + "TY").c_str(), l.twistAxis.y); k.f((p + "TZ").c_str(), l.twistAxis.z);
        k.f((p + "HX").c_str(), l.hingeAxis.x); k.f((p + "HY").c_str(), l.hingeAxis.y); k.f((p + "HZ").c_str(), l.hingeAxis.z);
    }
}

// The colliders a file from before the newer shapes knows about: the Cone, Convex Hull, Plane, Compound and Heightfield
// colliders save every setting (physics material, trigger filter, contact offset) in their own record, so the older
// "by index" records count only the rest and an index means the same thing before and after those shapes exist.
std::vector<Collider3D*> LegacyColliders3D(const GameObject& go) {
    std::vector<Collider3D*> out;
    for (Collider3D* c : go.GetComponents<Collider3D>()) {
        switch (c->shape()) {
        case Collider3D::Shape::Cone: case Collider3D::Shape::ConvexHull: case Collider3D::Shape::Compound:
        case Collider3D::Shape::Heightfield: case Collider3D::Shape::Plane: break;
        default: out.push_back(c);
        }
    }
    return out;
}
std::vector<Collider2D*> LegacyColliders2D(const GameObject& go) {
    std::vector<Collider2D*> out;
    for (Collider2D* c : go.GetComponents<Collider2D>()) if (c->shape() != Collider2D::Shape::Composite) out.push_back(c);
    return out;
}

// The crouch / prone options of a movement controller (StanceControl) and the
// stance it was saved in: one `<rec> key=value ...` line.
void WriteStanceControl(std::ostream& out, const char* rec, const StanceControl& sc) {
    out << "  " << rec;
    KvOut k{out};
    k.key("crouchKey", sc.crouchKey).key("proneKey", sc.proneKey).b("toggle", sc.toggle).b("prone", sc.allowProne)
     .b("resize", sc.resizeBody)("crouchSpeed", sc.crouchSpeed)("proneSpeed", sc.proneSpeed)
     ("crouchH", sc.crouchHeight)("proneH", sc.proneHeight)
     .b("dive", sc.diveOnProne)("diveHop", sc.diveHop)("diveSlide", sc.diveSlideTime);
    if (sc.Stance() != 0) k("stance", sc.Stance());   // saved crouched / prone (the collider is saved standing)
    out << "\n";
}
void ReadStanceControl(std::istream& in, StanceControl* sc) {
    KvIn k(in);
    if (!sc) return;
    k.key("crouchKey", sc->crouchKey); k.key("proneKey", sc->proneKey); k.b("toggle", sc->toggle);
    k.b("prone", sc->allowProne); k.b("resize", sc->resizeBody); k.f("crouchSpeed", sc->crouchSpeed);
    k.f("proneSpeed", sc->proneSpeed); k.f("crouchH", sc->crouchHeight); k.f("proneH", sc->proneHeight);
    k.b("dive", sc->diveOnProne); k.f("diveHop", sc->diveHop); k.f("diveSlide", sc->diveSlideTime);
    int st = 0; k.i("stance", st);
    if (st == 1 || st == 2) sc->Restore(st);
}

// Point lists inside one quoted key=value (line points, profile points).
std::string PointList(const std::vector<Vec2>& v) {
    std::ostringstream o; UseRoundTripFloats(o);
    for (std::size_t i = 0; i < v.size(); ++i) o << (i ? " " : "") << v[i].x << ' ' << v[i].y;
    return o.str();
}
std::string PointList(const std::vector<Vec3>& v) {
    std::ostringstream o; UseRoundTripFloats(o);
    for (std::size_t i = 0; i < v.size(); ++i) o << (i ? " " : "") << v[i].x << ' ' << v[i].y << ' ' << v[i].z;
    return o.str();
}
std::vector<float> ReadNumberList(const KvIn& k, const char* key) {
    std::vector<float> out;
    if (const std::string* s = k.find(key)) {
        std::istringstream in(*s);
        float f;
        while (in >> f) out.push_back(f);
    }
    return out;
}
// A width curve / colour gradient as `<p>n= <p>s= <p>0t= <p>0v= ...` keys.
void WriteCurveKv(KvOut& k, const std::string& p, const ParticleCurve& c) {
    k((p + "n").c_str(), c.count).b((p + "s").c_str(), c.smooth);
    for (int i = 0; i < c.count; ++i)
        k((p + std::to_string(i) + "t").c_str(), c.keys[i].time)((p + std::to_string(i) + "v").c_str(), c.keys[i].value);
}
void ReadCurveKv(const KvIn& k, const std::string& p, ParticleCurve& c) {
    if (!k.find((p + "n").c_str())) return;
    int n = 0; k.i((p + "n").c_str(), n);
    k.b((p + "s").c_str(), c.smooth);
    c.Clear();
    for (int i = 0; i < std::clamp(n, 0, ParticleCurve::kMaxKeys); ++i) {
        float t = 0.0f, v = 1.0f;
        k.f((p + std::to_string(i) + "t").c_str(), t); k.f((p + std::to_string(i) + "v").c_str(), v);
        c.Add(t, v);
    }
}
void WriteGradientKv(KvOut& k, const std::string& p, const ParticleGradient& g) {
    k((p + "n").c_str(), g.count);
    for (int i = 0; i < g.count; ++i)
        k((p + std::to_string(i) + "t").c_str(), g.stops[i].time).col((p + std::to_string(i)).c_str(), g.stops[i].color);
}
void ReadGradientKv(const KvIn& k, const std::string& p, ParticleGradient& g) {
    if (!k.find((p + "n").c_str())) return;
    int n = 0; k.i((p + "n").c_str(), n);
    g.Clear();
    for (int i = 0; i < std::clamp(n, 0, ParticleGradient::kMaxStops); ++i) {
        float t = 0.0f; Color c;
        k.f((p + std::to_string(i) + "t").c_str(), t); k.col((p + std::to_string(i)).c_str(), c);
        g.Add(t, c);
    }
}


// Read the three shared publish toggles trailing every stat_* line.
void ReadStatTail(std::istream& in, StatComponent* c) {
    int pp = 1, pb = 1, sm = 1; in >> pp >> pb >> sm;
    c->publishPrefs = (pp != 0); c->publishBar = (pb != 0); c->sendMessages = (sm != 0);
}

// Read an optional trailing UI anchor enum (added in a later format version).
// Older scenes lack the field, so we only consume it when an integer follows;
// otherwise the anchor stays at its default (TopLeft).
void ReadAnchor(std::istream& in, UIAnchor& anchor) {
    in >> std::ws;
    int ch = in.peek();
    if (ch >= '0' && ch <= '9') {
        int a = 0; in >> a;
        anchor = static_cast<UIAnchor>(a);
    }
}

// `compoff` records for every component that is switched off (per-component flags
// the individual records may not carry). Keyed by type name + occurrence.
void WriteDisabledComponents(std::ostream& out, const GameObject* go) {
    std::unordered_map<std::string, int> seen;
    for (const auto& c : go->Components()) {
        if (c.get() == static_cast<const Component*>(go->transform)) continue;
        const std::string key = ComponentSerializerRegistry::TypeKey(*c);
        const int occ = seen[key]++;
        if (!c->enabled) out << "  compoff " << Quote(key) << " " << occ << "\n";
    }
}

// The folder of the scene file being loaded (LoadFromFile), so mesh files it
// points at can be found next to it in an exported game.
thread_local std::string g_loadingSceneDir;

// A UTF-8 path as a file-system path for the streams below. On Windows a std::string handed to std::ifstream is read in
// the ANSI code page, so a scene or prefab under a folder such as "My Game \xC3\xBC" could not be opened or saved; the
// wide form names the right file and never throws (text that is not UTF-8 becomes U+FFFD, a name no file has).
std::filesystem::path FilePath(const std::string& utf8) {
#ifdef _WIN32
    return std::filesystem::path(utf8::ToWide(utf8));
#else
    return std::filesystem::path(utf8);
#endif
}

// Every component the file knows is saved per instance (the writers loop, the reader adds one
// component per record). A second record of a type this build does not save would still warn.
bool AllowsMultiple(const Component*) { return true; }

// Bring an object's components into the order of a `comporder` record: the Nth name of a type is the Nth
// component of that type; names the object has no component for are ignored, and components the record
// does not name keep their order after the named ones.
void ApplyComponentOrder(GameObject& go, const std::vector<std::string>& names) {
    std::unordered_map<std::string, std::vector<Component*>> byType;
    for (const auto& c : go.Components())
        if (c.get() != static_cast<Component*>(go.transform)) byType[ComponentSerializerRegistry::TypeKey(*c)].push_back(c.get());
    std::unordered_map<std::string, std::size_t> used;
    std::vector<Component*> want;
    for (const std::string& n : names) {
        auto it = byType.find(n);
        if (it == byType.end()) continue;
        std::size_t& u = used[n];
        if (u < it->second.size()) want.push_back(it->second[u++]);
    }
    go.ReorderComponents(want);
}

} // namespace sceneser
} // namespace okay
