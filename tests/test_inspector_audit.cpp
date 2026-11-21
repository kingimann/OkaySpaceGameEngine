// Inspector audit: walks every registered component and its properties (the property registry, 1.19), the same way the
// generic Inspector card does, and measures how clear each one is: shown settings with no explanation, numbers that
// clearly have a unit (seconds, metres, degrees, 0..1) but whose explanation names none, names that leak jargon or
// abbreviations, and settings whose explanation is word for word another setting's. It prints a table (worst 25 first)
// and then CHECKS the components that were cleaned up: every shown setting has an explanation, a unit where the name
// calls for one, and a plain name. Written from the registry's own API and the editor's TooltipFor(); no outside code.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "InspectorModel.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace okay;

static std::string Low(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
static bool Has(const std::string& h, const char* n) { return h.find(n) != std::string::npos; }

// The unit a number's NAME calls for ("" = none clearly).
static std::string UnitClassOf(const PropertyDesc& d) {
    if (d.type != PropertyType::Float && d.type != PropertyType::Int && d.type != PropertyType::Vec2 && d.type != PropertyType::Vec3) return "";
    const std::string n = Low(d.name);
    if (Has(n, "lowpass") || Has(n, "matchwidth") || Has(n, "texoffset")) return "";
    for (const char* k : {"duration", "delay", "lifetime", "interval", "cooldown", "period", "timeout", "fadein", "fadeout", "blendtime", "seconds"})
        if (Has(n, k)) return "seconds";
    for (const char* k : {"fieldofview", "angle", "yaw", "degrees"})
        if (Has(n, k)) return "degrees";
    for (const char* k : {"distance", "radius", "height", "width", "range", "mindist", "maxdist", "offset", "stepheight", "orthographicsize"})
        if (Has(n, k)) return "length";
    for (const char* k : {"volume", "opacity", "alpha", "bounciness", "friction"})
        if (Has(n, k)) return "fraction";
    return "";
}
static bool TipNamesUnit(const std::string& tip, const std::string& cls) {
    const std::string t = Low(tip);
    if (cls == "seconds") return Has(t, "second") || Has(t, "(s)");
    if (cls == "degrees") return Has(t, "degree");
    if (cls == "length")  return Has(t, "metre") || Has(t, "meter") || Has(t, "unit") || Has(t, "pixel") || Has(t, "world") || Has(t, "(m)");
    if (cls == "fraction") return Has(t, "0 to 1") || Has(t, "0..1") || Has(t, "0-1") || Has(t, "0 and 1") || Has(t, "percent") || Has(t, "%") || Has(t, "multiplier") || Has(t, "scale");
    return true;
}
// Jargon in a shown name: an abbreviation word.
static bool JargonName(const std::string& name) {
    static const std::set<std::string> bad = {"fov", "lod", "gi", "ao", "ssao", "hdr", "pcf", "ik", "fx", "dof", "mip", "dt", "idx", "val", "tmp", "cfg", "ptr", "msaa", "taa", "ssr", "pbr", "uv", "sdf"};
    const std::string pretty = Low(oked::PrettyName(name));
    std::string w;
    for (char c : pretty + " ") { if (c == ' ') { if (bad.count(w)) return true; w.clear(); } else w += c; }
    return false;
}

struct Row { std::string type; int shown = 0, noTip = 0, noUnit = 0, jargon = 0, dup = 0; };

static std::vector<const PropertyDesc*> Shown(const ComponentTypeInfo& i) {
    std::vector<const PropertyDesc*> v;
    for (const PropertyDesc& d : i.properties) {
        if (d.IsHidden() || d.IsRuntimeOnly() || d.name == "enabled") continue;   // what the generic card skips
        v.push_back(&d);
    }
    return v;
}

static Row Measure(const ComponentTypeInfo& i) {
    Row r; r.type = i.name;
    std::map<std::string, int> tipCount;
    auto shown = Shown(i);
    for (auto* d : shown) { const std::string t = oked::TooltipFor(i.name, *d); if (!t.empty()) ++tipCount[t]; }
    for (auto* d : shown) {
        ++r.shown;
        const std::string t = oked::TooltipFor(i.name, *d);
        const std::string cls = UnitClassOf(*d);
        if (t.empty()) { ++r.noTip; if (!cls.empty()) ++r.noUnit; }
        else {
            if (tipCount[t] > 1) ++r.dup;
            if (!cls.empty() && !TipNamesUnit(t, cls)) ++r.noUnit;
        }
        if (JargonName(d->name)) ++r.jargon;
    }
    return r;
}

static void TestMeasureTable() {
    RegisterBuiltinProperties();
    std::vector<Row> rows;
    int total = 0, noTip = 0, noUnit = 0, jargon = 0, dup = 0;
    for (auto& info : PropertyRegistry::All()) {
        Row r = Measure(*info);
        if (r.shown == 0) continue;
        total += r.shown; noTip += r.noTip; noUnit += r.noUnit; jargon += r.jargon; dup += r.dup;
        rows.push_back(r);
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.noTip != b.noTip ? a.noTip > b.noTip : a.type < b.type; });
    std::printf("INSPECTOR AUDIT (registry-driven components): %zu components, %d shown settings\n", rows.size(), total);
    std::printf("%-3s %-26s %6s %8s %7s %7s %5s\n", "#", "component", "shown", "no-text", "no-unit", "jargon", "dup");
    int n = 0;
    for (auto& r : rows) {
        if (++n > 25) break;
        std::printf("%-3d %-26s %6d %8d %7d %7d %5d\n", n, r.type.c_str(), r.shown, r.noTip, r.noUnit, r.jargon, r.dup);
    }
    std::printf("TOTAL: shown %d, no explanation %d, no unit %d, jargon names %d, repeated text %d\n", total, noTip, noUnit, jargon, dup);
    CHECK(total > 100);
}

// The components that were cleaned up: every shown setting is explained, has its unit, and a plain name.
static void TestCleanedComponents() {
    RegisterBuiltinProperties();
    const std::vector<std::string> fixed = {"GameObject", "Transform", "Rigidbody2D", "Rigidbody3D", "VirtualCamera", "CinemachineBrain", "Spawner", "TriggerZone", "NavMeshAgent", "UIButton", "TextRenderer", "UIImage",
        "Camera", "Light", "MeshRenderer", "SpriteRenderer", "AudioSource", "ParticleSystem",
        "Mover", "Spinner", "Lifetime", "CameraFollow", "Canvas", "UIPanel", "UIDamageNumbers", "DayNightCycle", "Draggable",
        "DropZone", "FollowTarget2D", "Collectible", "UIFrame", "UIFlexItem", "UILayoutGroup", "UIRadialProgress", "UIScrollView", "UIDraggable"};
    for (const std::string& name : fixed) {
        auto info = PropertyRegistry::FindByName(name);
        CHECK(info != nullptr);
        if (!info) continue;
        for (auto* d : Shown(*info)) {
            const std::string t = oked::TooltipFor(info->name, *d);
            if (t.empty()) std::fprintf(stderr, "  no explanation: %s.%s\n", name.c_str(), d->name.c_str());
            CHECK(!t.empty());
            const std::string cls = UnitClassOf(*d);
            if (!cls.empty() && !t.empty() && !TipNamesUnit(t, cls)) std::fprintf(stderr, "  no unit: %s.%s: %s\n", name.c_str(), d->name.c_str(), t.c_str());
            CHECK(cls.empty() || TipNamesUnit(t, cls));
            if (JargonName(d->name)) std::fprintf(stderr, "  jargon name: %s.%s\n", name.c_str(), d->name.c_str());
            CHECK(!JargonName(d->name));
        }
        { std::map<std::string, std::string> seen; for (auto* d : Shown(*info)) { std::string t = oked::TooltipFor(info->name, *d); if (t.empty()) continue; if (seen.count(t)) std::fprintf(stderr, "  same text: %s.%s and %s\n", name.c_str(), d->name.c_str(), seen[t].c_str()); seen[t] = d->name; } }
        Row r = Measure(*info);
        if (r.dup) std::fprintf(stderr, "  repeated text in %s: %d\n", name.c_str(), r.dup);
        CHECK(r.dup == 0);
    }
}

int main() {
    RUN_SUITE("Inspector audit table");  TestMeasureTable();
    RUN_SUITE("Cleaned components");     TestCleanedComponents();
    TEST_MAIN_RESULT();
}
