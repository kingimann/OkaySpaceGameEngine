#include "DevFieldDemo.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <cstdlib>
#include <iomanip>
#include <sstream>

using namespace okay;

namespace {

struct FieldDemo : public Component {
    bool on = true;
    int count = 3;
    float speed = 1.5f;
    float ratio = 0.5f;
    std::string label = "hello";
    Vec2 size{1, 2};
    Vec3 offset{0, 1, 0};
    Quat turn = Quat::Identity;
    Color tint{0.2f, 0.6f, 1.0f, 1.0f};
    int mode = 1;
    std::string icon;
    std::string sound;
    std::string target;   // GameObject::Guid
};

PropertyDesc Make(const char* name, PropertyType t, std::function<PropertyValue(const FieldDemo&)> get,
                  std::function<bool(FieldDemo&, const PropertyValue&)> set, const char* tip = "") {
    PropertyDesc d;
    d.name = name; d.type = t; d.tooltip = tip;
    d.getter = [get](const Component& c) { return get(static_cast<const FieldDemo&>(c)); };
    d.setter = [set](Component& c, const PropertyValue& v) { return set(static_cast<FieldDemo&>(c), v); };
    return d;
}

void RegisterFieldDemo() {
    ComponentTypeInfo info;
    info.name = "FieldDemo"; info.recordName = "fielddemo"; info.category = "Demo";
    info.type = std::type_index(typeid(FieldDemo));
    info.add = [](GameObject& go) -> Component* { return go.AddComponent<FieldDemo>(); };
    auto& p = info.properties;
    p.push_back(Make("enabled", PropertyType::Bool, [](const FieldDemo& c) { return PropertyValue::MakeBool(c.enabled); },
                     [](FieldDemo& c, const PropertyValue& v) { c.enabled = v.AsBool(); return true; }));
    p.push_back(Make("on", PropertyType::Bool, [](const FieldDemo& c) { return PropertyValue::MakeBool(c.on); },
                     [](FieldDemo& c, const PropertyValue& v) { c.on = v.AsBool(); return true; }, "A simple switch"));
    p.push_back(Make("count", PropertyType::Int, [](const FieldDemo& c) { return PropertyValue::MakeInt(c.count); },
                     [](FieldDemo& c, const PropertyValue& v) { c.count = (int)v.AsInt(); return true; }));
    p.back().hasRange = true; p.back().rangeMin = 0; p.back().rangeMax = 10;
    p.push_back(Make("speed", PropertyType::Float, [](const FieldDemo& c) { return PropertyValue::MakeFloat(c.speed); },
                     [](FieldDemo& c, const PropertyValue& v) { c.speed = v.AsFloat(); return true; }, "How fast it goes. Drag to change."));
    p.push_back(Make("ratio", PropertyType::Float, [](const FieldDemo& c) { return PropertyValue::MakeFloat(c.ratio); },
                     [](FieldDemo& c, const PropertyValue& v) { c.ratio = v.AsFloat(); return true; }));
    p.back().hasRange = true; p.back().rangeMin = 0; p.back().rangeMax = 1;
    p.push_back(Make("label", PropertyType::String, [](const FieldDemo& c) { return PropertyValue::MakeString(c.label); },
                     [](FieldDemo& c, const PropertyValue& v) { c.label = v.AsString(); return true; }));
    p.push_back(Make("size", PropertyType::Vec2, [](const FieldDemo& c) { return PropertyValue::MakeVec2(c.size); },
                     [](FieldDemo& c, const PropertyValue& v) { c.size = v.AsVec2(); return true; }));
    p.push_back(Make("offset", PropertyType::Vec3, [](const FieldDemo& c) { return PropertyValue::MakeVec3(c.offset); },
                     [](FieldDemo& c, const PropertyValue& v) { c.offset = v.AsVec3(); return true; }));
    p.push_back(Make("turn", PropertyType::Quat, [](const FieldDemo& c) { return PropertyValue::MakeQuat(c.turn); },
                     [](FieldDemo& c, const PropertyValue& v) { c.turn = v.AsQuat(); return true; }));
    p.push_back(Make("tint", PropertyType::Color, [](const FieldDemo& c) { return PropertyValue::MakeColor(c.tint); },
                     [](FieldDemo& c, const PropertyValue& v) { c.tint = v.AsColor(); return true; }));
    p.push_back(Make("mode", PropertyType::Enum, [](const FieldDemo& c) { return PropertyValue::MakeEnum(c.mode); },
                     [](FieldDemo& c, const PropertyValue& v) { c.mode = v.AsEnum(); return true; }));
    p.back().enumOptions = {"Off", "Steady", "Flicker"};
    p.push_back(Make("icon", PropertyType::AssetPath, [](const FieldDemo& c) { return PropertyValue::MakeAssetPath(c.icon); },
                     [](FieldDemo& c, const PropertyValue& v) { c.icon = v.AsString(); return true; }, "A picture"));
    p.back().assetKind = "image";
    p.push_back(Make("sound", PropertyType::AssetPath, [](const FieldDemo& c) { return PropertyValue::MakeAssetPath(c.sound); },
                     [](FieldDemo& c, const PropertyValue& v) { c.sound = v.AsString(); return true; }));
    p.back().assetKind = "audio";
    p.push_back(Make("target", PropertyType::ObjectRef, [](const FieldDemo& c) { return PropertyValue::MakeObjectRef(c.target); },
                     [](FieldDemo& c, const PropertyValue& v) { c.target = v.AsString(); return true; }, "The object to follow"));
    PropertyRegistry::Register(info);

    // Saved like any component, so undo and Save keep it: one line, every value quoted in the registry's text form.
    ComponentSerializerRegistry::Entry e;
    e.name = "fielddemo";
    e.write = [](const GameObject& go, std::ostream& out) {
        auto* c = go.GetComponent<FieldDemo>();
        if (!c) return;
        out << "  fielddemo";
        for (const PropertyDesc& d : PropertyRegistry::FindByName("FieldDemo")->properties)
            out << ' ' << std::quoted(d.getter(*c).ToString());
        out << "\n";
    };
    e.read = [](GameObject& go, std::istream& in) {
        auto* c = go.AddComponent<FieldDemo>();
        for (const PropertyDesc& d : PropertyRegistry::FindByName("FieldDemo")->properties) {
            std::string text;
            if (!(in >> std::quoted(text))) break;
            PropertyValue v;
            if (PropertyValue::FromString(d.type, text, v)) d.Set(*c, v);
        }
    };
    ComponentSerializerRegistry::Register(e);
}

} // namespace

namespace oked {

void DevFieldDemo(okay::editor::EditorState& ed) {
    static int state = 0;   // 0 not asked yet, 1 waiting for a scene, 2 done
    if (state == 2) return;
    if (state == 0) {
        if (!std::getenv("OKAY_DEV_FIELD_DEMO")) { state = 2; return; }
        RegisterFieldDemo();
        state = 1;
    }
    if (ed.scene().Objects().empty() || ed.isPlaying()) return;
    for (int i = 0; i < 2; ++i) {   // two, so multi-select edits show "mixed" values
        GameObject* g = ed.scene().CreateGameObject(i == 0 ? "Field Demo" : "Field Demo 2");
        auto* c = g->AddComponent<FieldDemo>();
        if (i == 1) { c->speed = 4.0f; c->label = "other"; c->tint = Color{1, 0.3f, 0.2f, 1}; c->offset = {0, 5, 0}; }
    }
    ed.dirty = true;
    state = 2;
}

} // namespace oked
