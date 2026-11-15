// Large values through the property registry. The built-in registrations bound many fields with
// kBig (BuiltinPropertiesShared.hpp). A registry write (Inspector edit, undo/redo of a snapshot, a Saveable's
// property paths on load, tween_prop, prefab edits) clamps to the range, so a scene that holds a value above
// the bound (a position 250 km out, 250000 health) would be cut down the first time it passed through the
// registry. For every registered number field whose range reaches the old kBig (100000) this plants 250000 (and
// -250000 where the range goes negative) with the raw setter, checks the field holds it, then writes the same
// value through PropertyRegistry::SetProperty and checks it is unchanged.
//
// It checks the fields bounded by kBig (a range that reaches 1e8 or more). At the old kBig of 1e5 this
// failed for 623 of 684 values (commit 47f886c); kBig is now 1e9. Some fields keep a deliberate literal
// 100000 cap (counts, levels, sort orders, resolutions, page numbers: Camera.depth, Canvas.sortOrder,
// ParticleSystem.maxParticles, Stats.level ...); they are listed in docs/properties.md and not checked here.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/PropertyRegistry.hpp"
#include <cmath>
#include <iostream>
#include <map>
#include <string>

using namespace okay;

static PropertyValue Big(PropertyType t, double v) {
    const float f = static_cast<float>(v);
    switch (t) {
        case PropertyType::Int:   return PropertyValue::MakeInt(static_cast<long long>(v));
        case PropertyType::Float: return PropertyValue::MakeFloat(f);
        case PropertyType::Vec2:  return PropertyValue::MakeVec2(Vec2{f, f});
        case PropertyType::Vec3:  return PropertyValue::MakeVec3(Vec3{f, f, f});
        case PropertyType::Vec4:  return PropertyValue::MakeVec4(Vec4{f, f, f, f});
        default: return PropertyValue();
    }
}

int main() {
    RUN_SUITE("registry: values beyond the old kBig bound are not clamped by a registry write");
    int cases = 0, failures = 0;
    std::map<std::string, int> perCategory;
    for (const auto& info : PropertyRegistry::All()) {
        if (!info || info->objectLevel || !info->add) continue;
        for (const PropertyDesc& d : info->properties) {
            if (d.type != PropertyType::Int && d.type != PropertyType::Float && d.type != PropertyType::Vec2 &&
                d.type != PropertyType::Vec3 && d.type != PropertyType::Vec4) continue;
            if (!d.hasRange || !d.setter || !d.getter || d.IsReadOnly()) continue;
            for (double v : {250000.0, -250000.0}) {
                if (v > 0 && d.rangeMax < 1.0e8) continue;      // a deliberate, smaller cap
                if (v < 0 && d.rangeMin > -1.0e8) continue;
                Scene s("s");
                GameObject* go = s.CreateGameObject("o");
                Component* c = info->add(*go);
                if (!c) continue;
                const PropertyValue big = Big(d.type, v);
                if (!d.setter(*c, big) || !(d.getter(*c) == big)) continue;   // the field itself cannot hold it
                ++cases;
                ++perCategory[info->category.empty() ? "(none)" : info->category];
                const std::string path = info->name + "." + d.name;
                PropertyRegistry::SetProperty(*go, path, big);
                const PropertyValue after = d.getter(*c);
                if (!(after == big)) {
                    ++failures;
                    if (failures <= 40) std::cerr << "  CLAMPED " << path << ": " << big.ToString() << " -> " << after.ToString() << "\n";
                }
            }
        }
    }
    std::cout << "  " << cases << " large values checked;";
    for (const auto& [cat, n] : perCategory) std::cout << " " << cat << " " << n;
    std::cout << "\n";
    if (failures) std::cerr << "  " << failures << " clamped\n";
    CHECK(failures == 0);
    CHECK(cases >= 500);
    TEST_MAIN_RESULT();
}
