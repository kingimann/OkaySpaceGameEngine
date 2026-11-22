// One reflection registry starts driving serialization (docs/engine/properties.md, "The generic scene record").
//
//  1. Coverage gate: every component class the engine headers declare has a PropertyRegistry entry, except the
//     names in registry_known_gaps.inc. That list may only shrink: a registered or vanished name in it fails too.
//  2. The generic path: every registered type round-trips through ComponentSerializerRegistry::WritePropertyFields
//     and ReadPropertyFields (registry -> scene text -> registry) with equal PropertyValues; a type registered with
//     RegisterFromProperties saves and loads through the scene file with no branch in SceneSerializer.cpp, reorder-safe,
//     unknown keys skipped with a note, missing keys left at their defaults.
//  3. Old files: a scene that main's serializer wrote (tests/golden/scene/registry_every_type_main.okayscene, one object
//     per registered type with its properties changed) loads and saves again byte for byte the same.
//
// `test_registry_serializer --write-golden <file>` writes that scene again (only when the format is meant to change).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/PropertyBuilder.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

// Component classes with no PropertyRegistry entry yet. May only shrink.
static const char* kKnownGaps[] = {
#include "registry_known_gaps.inc"
};

// Shared base classes: never added on their own (each concrete subclass is what an object holds).
static const char* kBaseClasses[] = {
    "Component", "Behaviour", "Collider2D", "Collider3D", "PolyShapeCollider2D", "ShapeCollider3D",
    "UIHudBase", "UIAppBase", "InventoryInteractable", "StatComponent",
};

// ---- 1. Every component class in the engine headers ---------------------------------------------------------

static std::string ReadFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Comments and string literals blanked out, so a "class X : public Y" in a comment does not count.
static std::string StripComments(const std::string& s) {
    std::string out(s.size(), ' ');
    for (std::size_t i = 0; i < s.size();) {
        if (s.compare(i, 2, "//") == 0) { while (i < s.size() && s[i] != '\n') ++i; continue; }
        if (s.compare(i, 2, "/*") == 0) {
            const std::size_t e = s.find("*/", i + 2);
            const std::size_t stop = e == std::string::npos ? s.size() : e + 2;
            for (; i < stop; ++i) if (s[i] == '\n') out[i] = '\n';
            continue;
        }
        if (s[i] == '"' || s[i] == '\'') {
            const char q = s[i++];
            while (i < s.size() && s[i] != q && s[i] != '\n') i += (s[i] == '\\') ? 2 : 1;
            ++i;
            continue;
        }
        out[i] = s[i];
        ++i;
    }
    return out;
}

static bool IdentChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// `class Name [final] : public A, B<T>, ns::C {` -> Name and {A, B, C}.
static void ScanClasses(const std::string& text, std::map<std::string, std::set<std::string>>& bases) {
    const std::string t = StripComments(text);
    for (std::size_t i = 0; i < t.size(); ++i) {
        std::size_t kw = 0;
        if (t.compare(i, 5, "class") == 0) kw = 5;
        else if (t.compare(i, 6, "struct") == 0) kw = 6;
        if (!kw || (i > 0 && IdentChar(t[i - 1])) || (i + kw < t.size() && IdentChar(t[i + kw]))) continue;
        std::size_t p = i + kw;
        auto skipWs = [&] { while (p < t.size() && std::isspace((unsigned char)t[p])) ++p; };
        auto ident = [&] { std::size_t b = p; while (p < t.size() && IdentChar(t[p])) ++p; return t.substr(b, p - b); };
        skipWs();
        std::string name = ident();
        if (name == "OKAY_API") { skipWs(); name = ident(); }
        if (name.empty()) continue;
        skipWs();
        const std::size_t save = p;
        if (ident() != "final") p = save;
        skipWs();
        if (p >= t.size() || t[p] != ':' || (p + 1 < t.size() && t[p + 1] == ':')) continue;
        ++p;
        const std::size_t end = t.find_first_of("{;", p);
        if (end == std::string::npos) continue;
        const std::string list = t.substr(p, end - p);
        int depth = 0;
        std::string cur;
        auto flush = [&] {
            std::istringstream ws(cur);
            std::string w, last;
            while (ws >> w) if (w != "public" && w != "protected" && w != "private" && w != "virtual") last = w;
            const std::size_t colon = last.rfind("::");
            if (colon != std::string::npos) last = last.substr(colon + 2);
            if (!last.empty()) bases[name].insert(last);
            cur.clear();
        };
        for (char c : list) {
            if (c == '<') ++depth;
            else if (c == '>') --depth;
            else if (c == ',' && depth == 0) flush();
            else if (depth == 0) cur += c;
        }
        flush();
    }
}

// Every class that derives (through any chain) from Component; Behaviour is Component's other name.
static std::set<std::string> EngineComponentClasses() {
    std::map<std::string, std::set<std::string>> bases;
    const fs::path root = fs::path(OKAY_SOURCE_DIR) / "engine" / "include";
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const std::string ext = it->path().extension().string();
        if (it->is_regular_file(ec) && (ext == ".hpp" || ext == ".h")) ScanClasses(ReadFile(it->path()), bases);
    }
    std::set<std::string> comps = {"Component", "Behaviour"};
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& [name, bs] : bases) {
            if (comps.count(name)) continue;
            for (const auto& b : bs)
                if (comps.count(b)) { comps.insert(name); grew = true; break; }
        }
    }
    return comps;
}

static bool Registered(const std::string& name) {
    auto info = PropertyRegistry::FindByName(name);
    return info && info->name == name;
}

static void TestCoverageGate() {
    RUN_SUITE("coverage gate: every component class has a registry entry (or is a known gap)");
    RegisterBuiltinProperties();
    const std::set<std::string> classes = EngineComponentClasses();
    const std::set<std::string> bases(std::begin(kBaseClasses), std::end(kBaseClasses));
    std::set<std::string> gaps;
    for (const char* g : kKnownGaps) {
        if (!*g) continue;
        CHECK(gaps.insert(g).second);   // listed once
    }
    CHECK(classes.size() > 250);   // the scan found the engine's components (about 300 classes)
    int registered = 0;
    std::vector<std::string> missing, stale;
    for (const auto& c : classes) {
        if (bases.count(c)) continue;
        if (Registered(c)) { ++registered; continue; }
        if (!gaps.count(c)) missing.push_back(c);
    }
    for (const auto& g : gaps)
        if (Registered(g) || !classes.count(g)) stale.push_back(g);
    for (const auto& b : bases) CHECK(classes.count(b) != 0);   // a base that is gone leaves the list
    for (const auto& m : missing) std::cerr << "    no PropertyRegistry entry: " << m << " (register it; do not add it to the gaps list)\n";
    for (const auto& s : stale) std::cerr << "    stale gap (registered now, or no such class): " << s << " - take it out of registry_known_gaps.inc\n";
    CHECK(missing.empty());
    CHECK(stale.empty());
    std::cout << "  " << classes.size() - bases.size() << " component classes: " << registered << " registered, "
              << gaps.size() << " known gaps, " << bases.size() << " base classes\n";
}

// ---- 2. The generic path ------------------------------------------------------------------------------------

static bool Authored(const PropertyDesc& d) {
    return !d.IsReadOnly() && !d.IsRuntimeOnly() && !d.IsDerived() && (d.flags & PropertyFlag::Structural) == 0;
}

// A different value the property accepts (as stored after clamping), or an invalid one.
static PropertyValue Vary(const PropertyDesc& d, const PropertyValue& cur, int salt) {
    std::vector<PropertyValue> cands;
    const float s = 0.25f * (float)(1 + salt % 3);
    switch (d.type) {
        case PropertyType::Bool: cands.push_back(PropertyValue::MakeBool(!cur.AsBool())); break;
        case PropertyType::Int:
            for (long long k : {1LL, -1LL, 3LL, -3LL}) cands.push_back(PropertyValue::MakeInt(cur.AsInt() + k));
            break;
        case PropertyType::Float:
            for (float f : {cur.AsFloat() + s, cur.AsFloat() * 0.5f, cur.AsFloat() - s, 0.375f}) cands.push_back(PropertyValue::MakeFloat(f));
            if (d.hasRange) cands.push_back(PropertyValue::MakeFloat((float)(d.rangeMin + (d.rangeMax - d.rangeMin) * 0.3)));
            break;
        case PropertyType::String:
            cands.push_back(PropertyValue::MakeString("Test \"value\" 7 \\ x=y"));
            cands.push_back(PropertyValue::MakeString("seven"));
            break;
        case PropertyType::AssetPath: cands.push_back(PropertyValue::MakeAssetPath("assets/test/file 7.dat")); break;
        case PropertyType::Enum:
            if (d.enumOptions.size() >= 2) cands.push_back(PropertyValue::MakeEnum((cur.AsEnum() + 1) % (int)d.enumOptions.size()));
            break;
        case PropertyType::Quat: cands.push_back(PropertyValue::MakeQuat(Quat{0.5f, 0.5f, 0.5f, 0.5f})); break;
        case PropertyType::Vec2: { const Vec2 v = cur.AsVec2(); cands.push_back(PropertyValue::MakeVec2(Vec2{v.x + s, v.y * 0.5f + 0.125f})); break; }
        case PropertyType::Vec3: { const Vec3 v = cur.AsVec3(); cands.push_back(PropertyValue::MakeVec3(Vec3{v.x + s, v.y * 0.5f + 0.125f, v.z - s})); break; }
        case PropertyType::Vec4: { const Vec4 v = cur.AsVec4(); cands.push_back(PropertyValue::MakeVec4(Vec4{v.x + s, v.y * 0.5f, v.z - s, v.w + 1.0f})); break; }
        case PropertyType::Color: {
            const Color c = cur.AsColor();
            cands.push_back(PropertyValue::MakeColor(Color{c.r * 0.5f, 0.25f + c.g * 0.5f, 1.0f - c.b * 0.5f, 0.75f}));
            cands.push_back(PropertyValue::MakeColor(Color{0.125f, 0.625f, 0.375f, 1.0f}));
            break;
        }
        default: break;   // ObjectRef: only Transform.parent, which is structural
    }
    for (const PropertyValue& c : cands) {
        PropertyValue out;
        if (d.Normalize(c, out) && out != cur) return out;
    }
    return PropertyValue();
}

// Change every authored property of `c` that takes a different value. Returns how many changed.
static int VaryAll(Component& c, const ComponentTypeInfo& info, int salt) {
    int n = 0;
    for (const PropertyDesc& d : info.properties) {
        if (!Authored(d) || d.name == "enabled") continue;
        const PropertyValue v = Vary(d, d.Get(c), salt + n);
        if (v.IsValid() && d.Set(c, v)) ++n;
    }
    return n;
}

static std::map<std::string, PropertyValue> Values(const Component& c, const ComponentTypeInfo& info) {
    std::map<std::string, PropertyValue> m;
    for (const PropertyDesc& d : info.properties)
        if (Authored(d) && d.name != "enabled") m[d.name] = d.Get(c);
    return m;
}

static void TestGenericRoundTripEveryType() {
    RUN_SUITE("generic path: every registered type, registry -> scene text -> registry");
    RegisterBuiltinProperties();
    int types = 0, props = 0, changed = 0;
    std::vector<std::string> bad;
    for (const auto& info : PropertyRegistry::All()) {
        if (info->objectLevel || !info->add) continue;
        Scene a("a");
        GameObject* go = a.CreateGameObject("O");
        Component* c = info->add(*go);
        if (!c) continue;
        ++types;
        changed += VaryAll(*c, *info, types);
        const auto want = Values(*c, *info);
        std::ostringstream out;
        ComponentSerializerRegistry::WritePropertyFields(*c, *info, out);
        const std::string text = out.str();

        Scene b("b");
        GameObject* go2 = b.CreateGameObject("O");
        Component* c2 = info->add(*go2);
        if (!c2) { bad.push_back(info->name + " (could not add)"); continue; }
        std::istringstream in(text);
        std::vector<std::string> notes;
        ComponentSerializerRegistry::ReadPropertyFields(*c2, *info, in, &notes);
        for (const auto& n : notes) std::cerr << "    " << info->name << " note: " << n << "\n";
        const auto got = Values(*c2, *info);
        int mism = 0;
        for (const auto& [k, v] : want) {
            ++props;
            auto it = got.find(k);
            if (it == got.end() || it->second != v) {
                if (mism++ < 3)
                    std::cerr << "    " << info->name << "." << k << ": wrote '" << v.ToString() << "' read '"
                              << (it == got.end() ? std::string("?") : it->second.ToString()) << "'\n";
            }
        }
        if (mism || !notes.empty()) bad.push_back(info->name);
    }
    for (const auto& n : bad) std::cerr << "    did not round-trip: " << n << "\n";
    CHECK(bad.empty());
    CHECK(types >= 150 && props >= 2000 && changed >= 1500);
    std::cout << "  " << types << " types, " << props << " properties (" << changed << " changed from the default), "
              << bad.size() << " failed\n";
}

// A component nobody wrote a serializer branch for: it saves through its registry entry alone.
namespace {
class RegSerWidget : public Component {
public:
    enum class Mode { Idle, Run, Jump };
    float speed = 1.0f;
    int count = 2;
    bool armed = false;
    std::string label = "widget";
    std::string notes;
    std::string clip;
    Vec3 dir{0, 1, 0};
    Color tint{1, 0, 0, 1};
    Mode mode = Mode::Idle;
    Quat spin;
    float live = 0.0f;   // runtime-only: never written
};
void RegisterWidget() {
    PropertyBuilder<RegSerWidget>("RegSerWidget", "regserwidget", "Test")
        .AddFloat("speed", &RegSerWidget::speed).Range(0, 100)
        .AddInt("count", &RegSerWidget::count).Range(0, 10)
        .AddBool("armed", &RegSerWidget::armed)
        .AddString("label", &RegSerWidget::label)
        .AddString("notes", &RegSerWidget::notes).Multiline()
        .AddAssetPath("clip", &RegSerWidget::clip, "audio")
        .AddVec3("dir", &RegSerWidget::dir)
        .AddColor("tint", &RegSerWidget::tint)
        .AddEnum("mode", &RegSerWidget::mode, {"Idle", "Run", "Jump"})
        .AddQuat("spin", &RegSerWidget::spin)
        .AddFloat("live", &RegSerWidget::live).RuntimeOnly()
        .Commit();
}
} // namespace

static std::string RecordLine(const std::string& text, const std::string& rec) {
    const std::size_t at = text.find("\n  " + rec + " ");
    if (at == std::string::npos) return {};
    const std::size_t end = text.find('\n', at + 1);
    return text.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
}

static void TestRegisterFromProperties() {
    RUN_SUITE("generic path: RegisterFromProperties saves a component with no serializer branch");
    RegisterBuiltinProperties();
    RegisterWidget();
    std::string err;
    CHECK(ComponentSerializerRegistry::RegisterFromProperties("RegSerWidget", &err));
    if (!err.empty()) std::cerr << "    " << err << "\n";
    // refusals: unknown type, the object-level entry, a record the chain already reads
    CHECK(!ComponentSerializerRegistry::RegisterFromProperties("NoSuchComponent", &err) && !err.empty());
    CHECK(!ComponentSerializerRegistry::RegisterFromProperties("GameObject", &err) && !err.empty());
    CHECK(!ComponentSerializerRegistry::RegisterFromProperties("Light", &err) && !err.empty());   // record "light" is the chain's
    CHECK(ComponentSerializerRegistry::Find("light") == nullptr);

    Scene a("a");
    GameObject* go = a.CreateGameObject("O");
    auto* w1 = go->AddComponent<RegSerWidget>();
    auto* w2 = go->AddComponent<RegSerWidget>();
    w1->speed = 12.5f; w1->count = 7; w1->armed = true; w1->label = "say \"hi\" = 1"; w1->notes = "line one\nline two";
    w1->clip = "Assets/a b.wav"; w1->dir = Vec3{0.25f, -2.0f, 1e-7f}; w1->tint = Color{0.1f, 0.2f, 0.3f, 0.4f};
    w1->mode = RegSerWidget::Mode::Jump; w1->spin = Quat{0.5f, 0.5f, 0.5f, 0.5f}; w1->live = 99.0f;
    w2->label = ""; w2->count = 0; w2->enabled = false;
    const std::string text = SceneSerializer::Serialize(a);
    const std::string line = RecordLine(text, "regserwidget");
    CHECK(!line.empty());
    CHECK(line.find("speed=12.5") != std::string::npos && line.find("live=") == std::string::npos && line.find("enabled=") == std::string::npos);

    Scene b("b");
    std::vector<std::string> warnings;
    CHECK(SceneSerializer::Deserialize(b, text, &err, &warnings));
    GameObject* back = b.Find("O");
    auto ws = back ? back->GetComponents<RegSerWidget>() : std::vector<RegSerWidget*>{};
    CHECK(ws.size() == 2);
    if (ws.size() == 2) {
        const auto info = PropertyRegistry::FindByName("RegSerWidget");
        CHECK(Values(*ws[0], *info) == Values(*w1, *info));
        CHECK(Values(*ws[1], *info) == Values(*w2, *info));
        CHECK(ws[0]->enabled && !ws[1]->enabled);
        CHECK(ws[0]->live == 0.0f);
        CHECK(ws[0]->notes == "line one\nline two");
    }
    CHECK(warnings.empty());
    for (const auto& w : warnings) std::cerr << "    warning: " << w << "\n";
    // stable on the second save
    CHECK(SceneSerializer::Serialize(b) == text);

    // reorder-safe; unknown keys skipped with a note; missing keys keep the default; a refused value keeps it too
    const std::string doc =
        "okayscene 3\ngameobject 0 \"O\"\n  active 1\n  transform 0 0 0 0 0 0 1 1 1 1\n"
        "  regserwidget mode=\"2 Jump\" futurekey=5 speed=3.5 count=\"many\" label=\"x y\" dir=\"1 2 3\"\n"
        "  regserwidget\n"
        "  parent -1\nend\n";
    Scene c("c");
    warnings.clear();
    CHECK(SceneSerializer::Deserialize(c, doc, &err, &warnings));
    auto cs = c.Find("O") ? c.Find("O")->GetComponents<RegSerWidget>() : std::vector<RegSerWidget*>{};
    CHECK(cs.size() == 2);
    if (cs.size() == 2) {
        CHECK(cs[0]->mode == RegSerWidget::Mode::Jump && cs[0]->speed == 3.5f && cs[0]->label == "x y");
        CHECK(cs[0]->dir.x == 1.0f && cs[0]->dir.y == 2.0f && cs[0]->dir.z == 3.0f);
        CHECK(cs[0]->count == 2 && cs[0]->armed == false && cs[0]->tint.r == 1.0f);   // defaults kept
        CHECK(cs[1]->speed == 1.0f && cs[1]->label == "widget");                         // an empty record: all defaults
    }
    bool sawUnknown = false, sawRefused = false;
    for (const auto& w : warnings) {
        if (w.find("futurekey") != std::string::npos) sawUnknown = true;
        if (w.find("count") != std::string::npos) sawRefused = true;
    }
    CHECK(sawUnknown && sawRefused);
    for (const auto& w : warnings) std::cout << "  note: " << w << "\n";
}

// Types this change moved onto the generic path: they were not saved at all before.
static void TestBuiltinGenericTypes() {
    RUN_SUITE("generic path: built-in types that use it save and load");
    RegisterBuiltinProperties();
    for (const char* rec : {"checkpointtrigger", "netavatarmotion", "timeddestroy"}) {
        CHECK(ComponentSerializerRegistry::Find(rec) != nullptr);
    }
    Scene a("a");
    GameObject* go = a.CreateGameObject("O");
    auto* cp = go->AddComponent<CheckpointTrigger>();
    cp->checkpoint = "gate 2"; cp->targetTag = "Hero"; cp->radius = 4.5f; cp->once = false; cp->deferred = false; cp->checkInterval = 0.25f;
    auto* nm = go->AddComponent<NetworkAvatarMotion>();
    nm->turnSpeed = 360.0f; nm->moveEpsilon = 0.08f; nm->runSpeed = 4.0f;
    go->AddComponent<TimedDestroy>()->life = 2.5f;
    Scene b("b");
    std::string err;
    CHECK(SceneSerializer::Deserialize(b, SceneSerializer::Serialize(a), &err));
    GameObject* back = b.Find("O");
    auto* cp2 = back ? back->GetComponent<CheckpointTrigger>() : nullptr;
    auto* nm2 = back ? back->GetComponent<NetworkAvatarMotion>() : nullptr;
    CHECK(cp2 && cp2->checkpoint == "gate 2" && cp2->targetTag == "Hero" && cp2->radius == 4.5f && !cp2->once && !cp2->deferred &&
          cp2->checkInterval == 0.25f);
    CHECK(nm2 && nm2->turnSpeed == 360.0f && nm2->moveEpsilon == 0.08f && nm2->runSpeed == 4.0f);
    auto* td2 = back ? back->GetComponent<TimedDestroy>() : nullptr;
    CHECK(td2 && td2->life == 2.5f);
}

// ---- 3. A scene main wrote loads and saves byte for byte the same -------------------------------------------

static const char* kGolden = "/tests/golden/scene/registry_every_type_main.okayscene";

static void BuildEveryTypeScene(Scene& s) {
    RegisterBuiltinProperties();
    int salt = 0;
    for (const auto& info : PropertyRegistry::All()) {
        if (info->objectLevel || !info->add) continue;
        // Water builds its surface mesh on the first load and saves every vertex colour of it (380 KB of text):
        // its record is covered by property_registry and duplicate_components instead.
        if (info->name == "Water") continue;
        GameObject* go = s.CreateGameObject(info->name);
        Component* c = info->add(*go);
        if (c) VaryAll(*c, *info, ++salt);
    }
}

static void TestMainSceneResavesUnchanged() {
    RUN_SUITE("a scene saved by main loads and saves byte for byte the same");
    const std::string path = std::string(OKAY_SOURCE_DIR) + kGolden;
    const std::string want = ReadFile(path);
    CHECK(!want.empty());
    if (want.empty()) { std::cerr << "    missing " << path << "\n"; return; }
    Scene s("golden");
    std::string err;
    std::vector<std::string> warnings;
    CHECK(SceneSerializer::Deserialize(s, want, &err, &warnings));
    if (!err.empty()) std::cerr << "    " << err << "\n";
    const std::string got = SceneSerializer::Serialize(s);
    if (got != want) {
        std::istringstream x(want), y(got);
        std::string lx, ly;
        int line = 0;
        while (true) {
            const bool hx = (bool)std::getline(x, lx), hy = (bool)std::getline(y, ly);
            ++line;
            if (!hx && !hy) break;
            if (!hx || !hy || lx != ly) {
                std::cerr << "    line " << line << "\n      main:  " << (hx ? lx.substr(0, 200) : "(end)") << "\n      now:   "
                          << (hy ? ly.substr(0, 200) : "(end)") << "\n";
                break;
            }
        }
    }
    CHECK(got == want);
    std::cout << "  " << s.Objects().size() << " objects, " << want.size() << " bytes, " << warnings.size() << " warnings\n";
}

int main(int argc, char** argv) {
    Log::console = false;
    if (argc == 3 && std::string(argv[1]) == "--write-golden") {
        Scene s("every_type");
        BuildEveryTypeScene(s);
        // Some components settle on their first load (Water makes its mesh, a lone controller becomes a Player
        // Controller): the file is the text after one load, which every later load and save keeps.
        Scene settled("every_type");
        if (!SceneSerializer::Deserialize(settled, SceneSerializer::Serialize(s))) return 1;
        std::ofstream f(argv[2], std::ios::binary);
        f << SceneSerializer::Serialize(settled);
        std::cout << "wrote " << argv[2] << "\n";
        return f ? 0 : 1;
    }
    TestCoverageGate();
    TestMainSceneResavesUnchanged();
    TestGenericRoundTripEveryType();
    TestRegisterFromProperties();
    TestBuiltinGenericTypes();
    TEST_MAIN_RESULT();
}
