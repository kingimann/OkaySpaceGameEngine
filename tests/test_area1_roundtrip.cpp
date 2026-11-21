// Area 1.5 / 1.19 audit: every component type the property registry knows is given non-default values for all of its
// authored properties, the scene is written to text and read back into a fresh scene, and every property must read
// the same. A property that is saved but not loaded (or loaded wrongly) shows up here by name.
#include "test_framework.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace okay;

namespace {

bool Authored(const PropertyDesc& d) {
    return !d.IsReadOnly() && !d.IsRuntimeOnly() && !d.IsDerived() && !(d.flags & PropertyFlag::Structural);
}

PropertyValue Perturb(const PropertyDesc& d, const PropertyValue& cur) {
    switch (d.type) {
        case PropertyType::Bool: return PropertyValue::MakeBool(!cur.AsBool());
        case PropertyType::Int: return PropertyValue::MakeInt(cur.AsInt() + 1);
        case PropertyType::Float: return PropertyValue::MakeFloat(cur.AsFloat() * 0.5f + 0.37f);
        case PropertyType::String: return PropertyValue::MakeString(cur.AsString() == "abc" ? "abd" : "abc");
        case PropertyType::AssetPath: return PropertyValue::MakeAssetPath("Assets/probe.dat");
        case PropertyType::Vec2: { Vec2 v = cur.AsVec2(); return PropertyValue::MakeVec2({v.x * 0.5f + 0.37f, v.y * 0.5f + 0.41f}); }
        case PropertyType::Vec3: { Vec3 v = cur.AsVec3(); return PropertyValue::MakeVec3({v.x * 0.5f + 0.37f, v.y * 0.5f + 0.41f, v.z * 0.5f + 0.43f}); }
        case PropertyType::Vec4: { Vec4 v = cur.AsVec4(); return PropertyValue::MakeVec4({v.x * 0.5f + 0.37f, v.y * 0.5f + 0.41f, v.z * 0.5f + 0.43f, v.w * 0.5f + 0.47f}); }
        case PropertyType::Color: { Color c = cur.AsColor(); return PropertyValue::MakeColor(Color(c.r * 0.5f + 0.21f, c.g * 0.5f + 0.17f, c.b * 0.5f + 0.13f, c.a * 0.5f + 0.25f)); }
        case PropertyType::Quat: return PropertyValue::MakeQuat(Quat::AngleAxis(33.0f, Vec3{0, 1, 0}));
        case PropertyType::Enum: {
            const int n = (int)d.enumOptions.size();
            if (n < 2) return cur;
            const int next = (cur.AsEnum() + 1) % n;
            return PropertyValue::MakeEnum(next, d.enumOptions[(size_t)next]);
        }
        default: return cur;
    }
}

}  // namespace

int main() {
    RUN_SUITE("area1_roundtrip");
    Log::console = false;

    int types = 0, props = 0, lost = 0;
    std::vector<std::string> report;
    for (const auto& info : PropertyRegistry::All()) {
        if (info->objectLevel || !info->add) continue;
        if (info->name == "Transform") continue;
        Scene a("a");
        GameObject* go = a.CreateGameObject("probe");
        Component* ca = info->add(*go);
        if (!ca) continue;
        ++types;
        std::vector<std::pair<const PropertyDesc*, PropertyValue>> want;
        for (const PropertyDesc& d : info->properties) {
            if (!Authored(d)) continue;
            PropertyValue cur = d.Get(*ca);
            if (!cur.IsValid()) continue;
            PropertyValue nv = Perturb(d, cur);
            if (nv != cur) d.Set(*ca, nv);
        }
        // What the component really holds now (setters clamp and may change other fields).
        for (const PropertyDesc& d : info->properties) {
            if (!Authored(d)) continue;
            PropertyValue v = d.Get(*ca);
            if (v.IsValid()) want.push_back({&d, v});
        }
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("b");
        std::string err;
        if (!SceneSerializer::Deserialize(b, text, &err)) {
            report.push_back(info->name + ": scene did not load back: " + err);
            ++lost;
            continue;
        }
        GameObject* gb = b.Find("probe");
        Component* cb = nullptr;
        if (gb) for (const auto& c : gb->Components()) { auto fi = PropertyRegistry::FindFor(*c); if (fi && fi->name == info->name) cb = c.get(); }
        if (!cb) { report.push_back(info->name + ": component not found after load"); ++lost; continue; }
        for (auto& w : want) {
            ++props;
            PropertyValue got = w.first->Get(*cb);
            if (got != w.second) {
                ++lost;
                report.push_back(info->name + "." + w.first->name + ": saved " + w.second.ToString() + " but loaded " + got.ToString());
            }
        }
    }
    std::printf("%d component types, %d properties compared, %d lost\n", types, props, lost);
    for (const auto& r : report) std::printf("  LOST %s\n", r.c_str());
    CHECK(types > 20);
    CHECK(lost == 0);
    TEST_MAIN_RESULT();
}
