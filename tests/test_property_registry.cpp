// Property reflection: PropertyValue text round trips, descriptors (type, range,
// enum checks), the registry (names, paths, sub-fields), the change hook,
// snapshot / diff / copy, every built-in property through get -> set -> get and
// through the scene serializer, and a third-party component registered with the
// PropertyBuilder.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/PropertyBuilder.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// ---- helpers -------------------------------------------------------------------

static bool RoundTrips(const PropertyValue& v) {
    PropertyValue back;
    if (!PropertyValue::FromString(v.Type(), v.ToString(), back)) return false;
    return back == v && back.ToString() == v.ToString();
}

static bool FloatBitsEqual(float a, float b) {
    if (std::isnan(a) && std::isnan(b)) return true;
    return std::memcmp(&a, &b, sizeof a) == 0;
}

// Equal, allowing what the scene file's six significant digits cost.
static bool Close(const PropertyValue& a, const PropertyValue& b) {
    if (a.Type() != b.Type()) return false;
    const int n = PropertyTypeComponentCount(a.Type());
    if (a.Type() == PropertyType::Float || n > 0) {
        const int count = n > 0 ? n : 1;
        for (int i = 0; i < count; ++i) {
            float x = 0, y = 0;
            if (n == 0) { x = a.AsFloat(); y = b.AsFloat(); }
            else {
                static const char* xyzw[] = {"x", "y", "z", "w"};
                static const char* rgba[] = {"r", "g", "b", "a"};
                const char* name = a.Type() == PropertyType::Color ? rgba[i] : xyzw[i];
                PropertyValue fa, fb;
                if (!a.GetSubField(name, fa) || !b.GetSubField(name, fb)) return false;
                x = fa.AsFloat(); y = fb.AsFloat();
            }
            if (std::fabs(x - y) > 1e-5f * std::max(1.0f, std::fabs(x))) return false;
        }
        return true;
    }
    return a == b;
}

// ---- PropertyValue ------------------------------------------------------------------

static void TestValueRoundTrip() {
    RUN_SUITE("PropertyValue text round trip");
    CHECK(RoundTrips(PropertyValue::MakeBool(true)));
    CHECK(RoundTrips(PropertyValue::MakeBool(false)));
    CHECK(RoundTrips(PropertyValue::MakeInt(0)));
    CHECK(RoundTrips(PropertyValue::MakeInt(-1)));
    CHECK(RoundTrips(PropertyValue::MakeInt(4294967295LL)));
    CHECK(RoundTrips(PropertyValue::MakeInt(9223372036854775807LL)));
    CHECK(RoundTrips(PropertyValue::MakeInt(-9223372036854775807LL - 1)));

    // Floats: every value must come back to the same bits, and read short.
    const float floats[] = {0.0f, -0.0f, 1.0f, -1.0f, 0.1f, 0.3f, 1.0f / 3.0f, 3.14159265f, 123456.789f, 1e-7f, 1e-30f,
                            1e30f, FLT_MAX, -FLT_MAX, FLT_MIN, FLT_EPSILON, 1.17549435e-38f, 1e-45f /*denormal*/,
                            16777216.0f, 0.99999994f, 2.5f, 100.0f};
    for (float f : floats) {
        PropertyValue v = PropertyValue::MakeFloat(f), back;
        CHECK(PropertyValue::FromString(PropertyType::Float, v.ToString(), back));
        CHECK(FloatBitsEqual(back.AsFloat(), f));
    }
    CHECK(PropertyValue::MakeFloat(0.1f).ToString() == "0.1");
    CHECK(PropertyValue::MakeFloat(2.5f).ToString() == "2.5");
    CHECK(PropertyValue::MakeFloat(100.0f).ToString() == "100");
    {   // a few thousand pseudo-random bit patterns
        unsigned state = 12345u;
        int bad = 0;
        for (int i = 0; i < 4000; ++i) {
            state = state * 1664525u + 1013904223u;
            float f; std::memcpy(&f, &state, sizeof f);
            if (std::isnan(f)) continue;
            PropertyValue v = PropertyValue::MakeFloat(f), back;
            if (!PropertyValue::FromString(PropertyType::Float, v.ToString(), back) || !FloatBitsEqual(back.AsFloat(), f)) ++bad;
        }
        CHECK(bad == 0);
    }
    {
        PropertyValue back;
        CHECK(PropertyValue::FromString(PropertyType::Float, PropertyValue::MakeFloat(INFINITY).ToString(), back) && std::isinf(back.AsFloat()) && back.AsFloat() > 0);
        CHECK(PropertyValue::FromString(PropertyType::Float, PropertyValue::MakeFloat(-INFINITY).ToString(), back) && std::isinf(back.AsFloat()) && back.AsFloat() < 0);
        CHECK(PropertyValue::FromString(PropertyType::Float, PropertyValue::MakeFloat(NAN).ToString(), back) && std::isnan(back.AsFloat()));
    }

    CHECK(RoundTrips(PropertyValue::MakeString("")));
    CHECK(RoundTrips(PropertyValue::MakeString("plain")));
    CHECK(RoundTrips(PropertyValue::MakeString("two words  and \"quotes\" and \\ slashes")));
    CHECK(RoundTrips(PropertyValue::MakeString("line one\nline two\ttab")));
    CHECK(RoundTrips(PropertyValue::MakeString("  leading and trailing  ")));
    CHECK(RoundTrips(PropertyValue::MakeString("caf\xC3\xA9 \xE2\x9C\x93")));
    CHECK(RoundTrips(PropertyValue::MakeAssetPath("assets/textures/rock 01.png")));
    CHECK(RoundTrips(PropertyValue::MakeObjectRef("0123456789abcdef0123456789abcdef")));
    CHECK(RoundTrips(PropertyValue::MakeObjectRef("")));

    CHECK(RoundTrips(PropertyValue::MakeVec2(Vec2{1.5f, -0.1f})));
    CHECK(RoundTrips(PropertyValue::MakeVec3(Vec3{0.1f, 2.5e10f, -1e-9f})));
    CHECK(RoundTrips(PropertyValue::MakeVec4(Vec4{1, 2, 3, 4})));
    CHECK(RoundTrips(PropertyValue::MakeQuat(Quat{0.5f, 0.5f, 0.5f, 0.5f})));
    CHECK(RoundTrips(PropertyValue::MakeQuat(Quat::Euler(10.0f, 20.0f, 30.0f))));
    CHECK(RoundTrips(PropertyValue::MakeColor(Color{0.25f, 0.5f, 0.125f, 0.75f})));
    CHECK(RoundTrips(PropertyValue::MakeEnum(2)));
    CHECK(RoundTrips(PropertyValue::MakeEnum(1, "Point")));
    CHECK(RoundTrips(PropertyValue::MakeEnum(-3, "Spot Light")));
    CHECK(PropertyValue::MakeVec3(Vec3{1, 2.5f, -3}).ToString() == "1 2.5 -3");
    CHECK(PropertyValue::MakeEnum(1, "Point").ToString() == "1 Point");
    CHECK(PropertyValue().ToString().empty());

    // Friendly input forms that read to the same value.
    PropertyValue v;
    CHECK(PropertyValue::FromString(PropertyType::Bool, " YES ", v) && v.AsBool());
    CHECK(PropertyValue::FromString(PropertyType::Bool, "0", v) && !v.AsBool());
    CHECK(PropertyValue::FromString(PropertyType::Vec3, "(1, 2, 3)", v) && v.AsVec3().z == 3.0f);
    CHECK(PropertyValue::FromString(PropertyType::Vec3, "1,2,3", v) && v.AsVec3().y == 2.0f);
    CHECK(PropertyValue::FromString(PropertyType::Vec2, "[ 4  5 ]", v) && v.AsVec2().x == 4.0f);
    CHECK(PropertyValue::FromString(PropertyType::Color, "#FF8000", v) && std::fabs(v.AsColor().g - 128.0f / 255.0f) < 1e-6f && v.AsColor().a == 1.0f);
    CHECK(PropertyValue::FromString(PropertyType::Color, "#00000080", v) && std::fabs(v.AsColor().a - 128.0f / 255.0f) < 1e-6f);
    CHECK(PropertyValue::FromString(PropertyType::Float, "  1e2 ", v) && v.AsFloat() == 100.0f);
    CHECK(PropertyValue::FromString(PropertyType::Int, "+42", v) && v.AsInt() == 42);
    CHECK(PropertyValue::FromString(PropertyType::Enum, "3", v) && v.AsEnum() == 3 && v.EnumName().empty());
    CHECK(PropertyValue::FromString(PropertyType::Enum, "3 Some Name", v) && v.AsEnum() == 3 && v.EnumName() == "Some Name");
}

static void TestValueBadInput() {
    RUN_SUITE("PropertyValue bad input never throws or writes");
    struct Case { PropertyType type; const char* text; };
    const Case bad[] = {
        {PropertyType::Bool, ""}, {PropertyType::Bool, "maybe"}, {PropertyType::Bool, "2"}, {PropertyType::Bool, "true false"},
        {PropertyType::Int, ""}, {PropertyType::Int, "abc"}, {PropertyType::Int, "1.5"}, {PropertyType::Int, "12abc"}, {PropertyType::Int, "--1"},
        {PropertyType::Int, "+"}, {PropertyType::Int, "0x10"}, {PropertyType::Int, "99999999999999999999"}, {PropertyType::Int, "1 2"},
        {PropertyType::Float, ""}, {PropertyType::Float, " "}, {PropertyType::Float, "abc"}, {PropertyType::Float, "1.2.3"}, {PropertyType::Float, "1e"},
        {PropertyType::Float, "1e999"}, {PropertyType::Float, "0x1p3"}, {PropertyType::Float, "1f"}, {PropertyType::Float, "--1"},
        {PropertyType::Float, "."}, {PropertyType::Float, "1 2"}, {PropertyType::Float, "1,5"},
        {PropertyType::Vec2, ""}, {PropertyType::Vec2, "1"}, {PropertyType::Vec2, "1 2 3"}, {PropertyType::Vec2, "a b"}, {PropertyType::Vec2, "1,,2"},
        {PropertyType::Vec3, "1 2"}, {PropertyType::Vec3, "1 2 x"}, {PropertyType::Vec3, "(1 2 3"}, {PropertyType::Vec3, "1 2 3 4"}, {PropertyType::Vec3, "1;2;3"},
        {PropertyType::Vec4, "1 2 3"}, {PropertyType::Quat, "0 0 0"}, {PropertyType::Quat, "0 0 0 1 1"},
        {PropertyType::Color, "#12345"}, {PropertyType::Color, "#GG0000"}, {PropertyType::Color, "1 2 3"}, {PropertyType::Color, "red"},
        {PropertyType::Enum, ""}, {PropertyType::Enum, "Point"}, {PropertyType::Enum, "1.5"}, {PropertyType::Enum, "99999999999"},
        {PropertyType::None, "x"},
    };
    int rejected = 0;
    for (const Case& c : bad) {
        PropertyValue out = PropertyValue::MakeInt(777);   // must stay as it was
        const bool ok = PropertyValue::FromString(c.type, c.text, out);
        CHECK(!ok);
        CHECK(out == PropertyValue::MakeInt(777));
        if (!ok) ++rejected;
    }
    CHECK(rejected == (int)(sizeof bad / sizeof bad[0]));

    PropertyValue wrong = PropertyValue::MakeFloat(2.0f);
    CHECK(wrong.AsInt() == 0 && !wrong.AsBool() && wrong.AsString().empty() && wrong.AsVec3().x == 0.0f);
    CHECK(PropertyValue().AsFloat() == 0.0f);
    PropertyValue unchanged = PropertyValue::MakeFloat(1.0f), sub;
    CHECK(!unchanged.GetSubField("x", sub));
    CHECK(!PropertyValue::MakeVec3(Vec3{1, 2, 3}).GetSubField("w", sub));
    CHECK(!PropertyValue::MakeVec3(Vec3{1, 2, 3}).GetSubField("r", sub));
    CHECK(PropertyValue::MakeColor(Color{0.1f, 0.2f, 0.3f, 0.4f}).GetSubField("A", sub) && sub.AsFloat() == 0.4f);

    PropertyType t;
    CHECK(PropertyTypeFromName("vec3", t) && t == PropertyType::Vec3);
    CHECK(!PropertyTypeFromName("Matrix", t));
    CHECK(std::string(PropertyTypeName(PropertyType::AssetPath)) == "AssetPath");

    CHECK(PropertyValue::MakeFloat(NAN) == PropertyValue::MakeFloat(NAN));
    CHECK(PropertyValue::MakeFloat(0.0f) == PropertyValue::MakeFloat(-0.0f));
    CHECK(PropertyValue::MakeInt(1) != PropertyValue::MakeFloat(1.0f));
    CHECK(PropertyValue::MakeEnum(1, "A") == PropertyValue::MakeEnum(1, "B"));
}

// ---- PropertyDesc ---------------------------------------------------------------------------

namespace {
struct Probe : Component {
    float f = 1.0f;
    int i = 5;
    bool b = false;
    std::string s = "x";
    Vec3 v{1, 2, 3};
    Quat q;
    Color c;
    int mode = 1;
    std::string ref;
};
}

static PropertyDesc MakeDesc(PropertyType type) {
    PropertyDesc d;
    d.name = "p";
    d.type = type;
    d.getter = [type](const Component& c) {
        const Probe& p = static_cast<const Probe&>(c);
        switch (type) {
            case PropertyType::Float: return PropertyValue::MakeFloat(p.f);
            case PropertyType::Int: return PropertyValue::MakeInt(p.i);
            case PropertyType::Bool: return PropertyValue::MakeBool(p.b);
            case PropertyType::String: return PropertyValue::MakeString(p.s);
            case PropertyType::Vec3: return PropertyValue::MakeVec3(p.v);
            case PropertyType::Quat: return PropertyValue::MakeQuat(p.q);
            case PropertyType::Color: return PropertyValue::MakeColor(p.c);
            case PropertyType::Enum: return PropertyValue::MakeEnum(p.mode);
            case PropertyType::ObjectRef: return PropertyValue::MakeObjectRef(p.ref);
            default: return PropertyValue();
        }
    };
    d.setter = [type](Component& c, const PropertyValue& v) {
        Probe& p = static_cast<Probe&>(c);
        switch (type) {
            case PropertyType::Float: p.f = v.AsFloat(); break;
            case PropertyType::Int: p.i = (int)v.AsInt(); break;
            case PropertyType::Bool: p.b = v.AsBool(); break;
            case PropertyType::String: p.s = v.AsString(); break;
            case PropertyType::Vec3: p.v = v.AsVec3(); break;
            case PropertyType::Quat: p.q = v.AsQuat(); break;
            case PropertyType::Color: p.c = v.AsColor(); break;
            case PropertyType::Enum: p.mode = v.AsEnum(); break;
            case PropertyType::ObjectRef: p.ref = v.AsString(); break;
            default: return false;
        }
        return true;
    };
    return d;
}

static void TestDescriptor() {
    RUN_SUITE("PropertyDesc validation");
    Probe p;
    {   // type must match exactly
        PropertyDesc d = MakeDesc(PropertyType::Float);
        CHECK(!d.Set(p, PropertyValue::MakeInt(3)));
        CHECK(!d.Set(p, PropertyValue::MakeBool(true)));
        CHECK(!d.Set(p, PropertyValue::MakeString("3")));
        CHECK(!d.Set(p, PropertyValue()));
        CHECK(p.f == 1.0f);
        CHECK(d.Set(p, PropertyValue::MakeFloat(3.0f)) && p.f == 3.0f);
        CHECK(!d.Set(p, PropertyValue::MakeFloat(NAN)));
        CHECK(!d.Set(p, PropertyValue::MakeFloat(INFINITY)));
        CHECK(!d.Set(p, PropertyValue::MakeFloat(-INFINITY)));
        CHECK(p.f == 3.0f);
    }
    {   // clamp
        PropertyDesc d = MakeDesc(PropertyType::Float);
        d.hasRange = true; d.rangeMin = 0.0; d.rangeMax = 10.0;
        CHECK(d.Set(p, PropertyValue::MakeFloat(25.0f)) && p.f == 10.0f);
        CHECK(d.Set(p, PropertyValue::MakeFloat(-4.0f)) && p.f == 0.0f);
        CHECK(d.Set(p, PropertyValue::MakeFloat(7.5f)) && p.f == 7.5f);
        PropertyValue n;
        CHECK(d.Normalize(PropertyValue::MakeFloat(11.0f), n) && n.AsFloat() == 10.0f);
        // reject instead
        d.rejectOutOfRange = true;
        p.f = 2.0f;
        CHECK(!d.Set(p, PropertyValue::MakeFloat(25.0f)) && p.f == 2.0f);
        CHECK(!d.Set(p, PropertyValue::MakeFloat(-0.5f)) && p.f == 2.0f);
        CHECK(d.Set(p, PropertyValue::MakeFloat(10.0f)) && p.f == 10.0f);   // the bounds themselves are in range
        CHECK(d.Set(p, PropertyValue::MakeFloat(0.0f)) && p.f == 0.0f);
    }
    {   // ints, including a big unsigned range
        PropertyDesc d = MakeDesc(PropertyType::Int);
        d.hasRange = true; d.rangeMin = 0; d.rangeMax = 31;
        CHECK(d.Set(p, PropertyValue::MakeInt(99)) && p.i == 31);
        CHECK(d.Set(p, PropertyValue::MakeInt(-99)) && p.i == 0);
        CHECK(!d.Set(p, PropertyValue::MakeFloat(3.0f)));
        PropertyDesc big = MakeDesc(PropertyType::Int);
        big.hasRange = true; big.rangeMin = 0; big.rangeMax = 4294967295.0;
        PropertyValue n;
        CHECK(big.Normalize(PropertyValue::MakeInt(4294967295LL), n) && n.AsInt() == 4294967295LL);
        CHECK(big.Normalize(PropertyValue::MakeInt(4294967296LL), n) && n.AsInt() == 4294967295LL);
    }
    {   // vectors are checked per component
        PropertyDesc d = MakeDesc(PropertyType::Vec3);
        d.hasRange = true; d.rangeMin = 0.0; d.rangeMax = 1.0;
        CHECK(d.Set(p, PropertyValue::MakeVec3(Vec3{-1, 0.5f, 3})));
        CHECK(p.v.x == 0.0f && p.v.y == 0.5f && p.v.z == 1.0f);
        CHECK(!d.Set(p, PropertyValue::MakeVec3(Vec3{0, NAN, 0})));
        CHECK(!d.Set(p, PropertyValue::MakeVec2(Vec2{0, 0})));
        CHECK(p.v.z == 1.0f);
    }
    {   // quaternions
        PropertyDesc d = MakeDesc(PropertyType::Quat);
        PropertyValue n;
        CHECK(d.Normalize(PropertyValue::MakeQuat(Quat{0, 0, 0, 2}), n) && n.AsQuat().w == 1.0f);
        CHECK(!d.Normalize(PropertyValue::MakeQuat(Quat{0, 0, 0, 0}), n));
        CHECK(!d.Normalize(PropertyValue::MakeQuat(Quat{NAN, 0, 0, 1}), n));
        const Quat unit{0.5f, 0.5f, 0.5f, 0.5f};
        CHECK(d.Normalize(PropertyValue::MakeQuat(unit), n) && n == PropertyValue::MakeQuat(unit));   // a unit quaternion is stored untouched
        CHECK(!d.Normalize(PropertyValue::MakeVec4(Vec4{0, 0, 0, 1}), n));
    }
    {   // enums
        PropertyDesc d = MakeDesc(PropertyType::Enum);
        d.enumOptions = {"A", "B", "C"};
        CHECK(d.Set(p, PropertyValue::MakeEnum(2)) && p.mode == 2);
        CHECK(!d.Set(p, PropertyValue::MakeEnum(3)) && p.mode == 2);
        CHECK(!d.Set(p, PropertyValue::MakeEnum(-1)) && p.mode == 2);
        CHECK(!d.Set(p, PropertyValue::MakeInt(1)) && p.mode == 2);
        CHECK(d.Get(p).EnumName() == "C");
        PropertyValue v;
        CHECK(d.ParseText("b", v) && v.AsEnum() == 1 && v.EnumName() == "B");
        CHECK(d.ParseText("2", v) && v.AsEnum() == 2);
        CHECK(d.ParseText("0 A", v) && v.AsEnum() == 0);
        CHECK(!d.ParseText("D", v));
        CHECK(d.FormatText(PropertyValue::MakeEnum(1)) == "B");
        CHECK(d.FormatText(PropertyValue::MakeEnum(9)) == "9");
    }
    {   // strings: no control characters unless multiline
        PropertyDesc d = MakeDesc(PropertyType::String);
        CHECK(d.Set(p, PropertyValue::MakeString("hello world")) && p.s == "hello world");
        CHECK(!d.Set(p, PropertyValue::MakeString("a\nb")) && p.s == "hello world");
        CHECK(!d.Set(p, PropertyValue::MakeString(std::string("a\0b", 3))) && p.s == "hello world");
        d.multiline = true;
        CHECK(d.Set(p, PropertyValue::MakeString("a\nb")) && p.s == "a\nb");
    }
    {   // object references are empty or an id
        PropertyDesc d = MakeDesc(PropertyType::ObjectRef);
        CHECK(d.Set(p, PropertyValue::MakeObjectRef("00ff-aa")) && p.ref == "00ff-aa");
        CHECK(d.Set(p, PropertyValue::MakeObjectRef("")) && p.ref.empty());
        CHECK(!d.Set(p, PropertyValue::MakeObjectRef("Player")));
        CHECK(!d.Set(p, PropertyValue::MakeObjectRef("has space")));
        CHECK(!d.Set(p, PropertyValue::MakeObjectRef(std::string(65, 'a'))));
        CHECK(!d.Set(p, PropertyValue::MakeString("00ff")));
    }
    {   // read-only and a refusing setter leave things alone
        PropertyDesc d = MakeDesc(PropertyType::Bool);
        d.flags = PropertyFlag::ReadOnly;
        CHECK(!d.Set(p, PropertyValue::MakeBool(true)) && !p.b);
        d.flags = 0;
        d.setter = [](Component&, const PropertyValue&) { return false; };
        CHECK(!d.Set(p, PropertyValue::MakeBool(true)) && !p.b);
        PropertyDesc none;
        CHECK(!none.Set(p, PropertyValue::MakeBool(true)));
        CHECK(!none.Get(p).IsValid());
    }
}

// ---- Registry ------------------------------------------------------------------------------------

static void TestRegistryBasics() {
    RUN_SUITE("registry: built-ins, lookup");
    RegisterBuiltinProperties();
    const std::size_t count = PropertyRegistry::All().size();
    RegisterBuiltinProperties();   // idempotent
    CHECK(PropertyRegistry::All().size() == count);
    CHECK(count >= 20);

    const char* expected[] = {"GameObject", "Transform", "Camera", "Light", "SpriteRenderer", "MeshRenderer", "Rigidbody2D", "Rigidbody3D",
        "BoxCollider2D", "CircleCollider2D", "CapsuleCollider2D", "EdgeCollider2D", "PolygonCollider2D",
        "BoxCollider3D", "SphereCollider3D", "CapsuleCollider3D", "CylinderCollider3D", "MeshCollider3D", "AudioSource", "ParticleSystem",
        "Mover", "Spinner", "Lifetime", "CameraFollow", "Canvas", "UIPanel"};
    for (const char* n : expected) CHECK(PropertyRegistry::FindByName(n) != nullptr);

    auto light = PropertyRegistry::FindByName("Light");
    CHECK(light && light->name == "Light" && light->recordName == "light");
    CHECK(PropertyRegistry::FindByName("light") == light);          // record name
    CHECK(PropertyRegistry::FindByName("LIGHT") == light);          // any case
    CHECK(PropertyRegistry::FindByType(typeid(Light)) == light);
    CHECK(PropertyRegistry::FindByName("sprite") == PropertyRegistry::FindByName("SpriteRenderer"));
    CHECK(PropertyRegistry::FindByName("rigidbody2d")->name == "Rigidbody2D");
    CHECK(PropertyRegistry::FindByName("nope") == nullptr);
    CHECK(PropertyRegistry::FindByName("") == nullptr);

    Scene s("s");
    GameObject* go = s.CreateGameObject("go");
    Light* l = go->AddComponent<Light>();
    CHECK(PropertyRegistry::FindFor(*l) == light);
    CHECK(PropertyRegistry::FindFor(*go->transform)->name == "Transform");
    // an exact-type match: a subclass is not silently treated as its base
    struct SubLight : Light {};
    SubLight* sl = go->AddComponent<SubLight>();
    CHECK(PropertyRegistry::FindFor(*sl) == nullptr);

    CHECK(light->Find("intensity") != nullptr);
    CHECK(light->Find("Intensity") == light->Find("intensity"));
    CHECK(light->Find("nothing") == nullptr);
    CHECK(light->Find("intensity")->type == PropertyType::Float);
    CHECK(light->Find("intensity")->group.empty());
    CHECK(light->Find("castShadows")->group == "Shadows");
    CHECK(light->Find("type")->enumOptions.size() == 4);
    CHECK(light->Find("intensity")->defaultValue == PropertyValue::MakeFloat(1.0f));
    CHECK(light->Find("temperature")->defaultValue == PropertyValue::MakeFloat(6500.0f));
    CHECK(PropertyRegistry::FindByName("Transform")->add == nullptr);
    CHECK(PropertyRegistry::FindByName("Light")->add != nullptr);

    // Names match the serializer's type key, and every record name is one the
    // scene writer uses (checked against real output in the cross-check below).
    for (const auto& info : PropertyRegistry::All()) {
        if (info->objectLevel || !info->add) continue;
        Scene tmp("t");
        GameObject* g = tmp.CreateGameObject("g");
        Component* c = info->add(*g);
        CHECK(c != nullptr);
        if (c) CHECK(ComponentSerializerRegistry::TypeKey(*c) == info->name);
        if (c) CHECK(PropertyRegistry::FindFor(*c) == info);
    }
}

static void TestThreadSafeRegistration() {
    RUN_SUITE("registry: concurrent first use");
    PropertyRegistry::Clear();   // only the built-ins from here on
    const std::size_t count = PropertyRegistry::All().size();
    CHECK(count >= 26);

    // A fresh table (as in a new process), four threads racing to be the first user:
    // every one of them must see the complete table, and it must hold each entry once.
    for (int round = 0; round < 5; ++round) {
        PropertyRegistry::Clear();
        std::vector<std::size_t> seen(4, 0);
        std::vector<int> found(4, 0);
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&, t] {
                if (t % 2 == 0) RegisterBuiltinProperties();
                found[t] = (PropertyRegistry::FindByName("Camera") && PropertyRegistry::FindByName("ParticleSystem")) ? 1 : 0;
                seen[t] = PropertyRegistry::All().size();
            });
        for (auto& th : threads) th.join();
        for (int t = 0; t < 4; ++t) { CHECK(found[t] == 1); CHECK(seen[t] == count); }
        CHECK(PropertyRegistry::All().size() == count);
    }
    // Registering from many threads at once is safe too.
    std::vector<std::thread> writers;
    for (int t = 0; t < 4; ++t)
        writers.emplace_back([t] {
            for (int i = 0; i < 50; ++i) {
                ComponentTypeInfo info;
                info.name = "Race" + std::to_string(t);
                info.type = std::type_index(typeid(PropertyRegistry)) ;
                PropertyRegistry::Register(info);
                PropertyRegistry::FindByName("Light");
            }
        });
    for (auto& th : writers) th.join();
    PropertyRegistry::Clear();   // forget the race entries
    CHECK(PropertyRegistry::All().size() == count);
}

static void TestPaths() {
    RUN_SUITE("paths and sub-fields");
    Scene s("s");
    GameObject* go = s.CreateGameObject("Thing");
    Light* light = go->AddComponent<Light>();
    Camera* cam = go->AddComponent<Camera>();
    SpriteRenderer* sprite = go->AddComponent<SpriteRenderer>();

    CHECK(PropertyRegistry::GetProperty(*go, "Light.intensity") == PropertyValue::MakeFloat(1.0f));
    CHECK(PropertyRegistry::SetProperty(*go, "Light.intensity", PropertyValue::MakeFloat(2.5f)));
    CHECK(light->intensity == 2.5f);
    CHECK(PropertyRegistry::GetProperty(*go, "light.INTENSITY") == PropertyValue::MakeFloat(2.5f));   // record name, any case
    CHECK(PropertyRegistry::GetProperty(*go, "Camera.fieldOfView").AsFloat() == cam->fieldOfView);

    // transform
    CHECK(PropertyRegistry::SetProperty(*go, "Transform.position", PropertyValue::MakeVec3(Vec3{1, 2, 3})));
    CHECK(go->transform->localPosition.y == 2.0f);
    CHECK(PropertyRegistry::GetProperty(*go, "Transform.position.x") == PropertyValue::MakeFloat(1.0f));
    CHECK(PropertyRegistry::SetProperty(*go, "Transform.position.y", PropertyValue::MakeFloat(9.0f)));
    CHECK(go->transform->localPosition.x == 1.0f && go->transform->localPosition.y == 9.0f && go->transform->localPosition.z == 3.0f);
    CHECK(PropertyRegistry::SetProperty(*go, "Transform.scale.z", PropertyValue::MakeFloat(4.0f)) && go->transform->localScale.z == 4.0f);
    CHECK(PropertyRegistry::GetProperty(*go, "Transform.POSITION.Z") == PropertyValue::MakeFloat(3.0f));

    // color sub-fields use r g b a
    CHECK(PropertyRegistry::SetProperty(*go, "Light.color", PropertyValue::MakeColor(Color{0.1f, 0.2f, 0.3f, 0.4f})));
    CHECK(PropertyRegistry::GetProperty(*go, "Light.color.g") == PropertyValue::MakeFloat(0.2f));
    CHECK(PropertyRegistry::SetProperty(*go, "Light.color.b", PropertyValue::MakeFloat(0.9f)));
    CHECK(light->color.r == 0.1f && light->color.g == 0.2f && light->color.b == 0.9f && light->color.a == 0.4f);
    CHECK(PropertyRegistry::SetProperty(*go, "SpriteRenderer.size.x", PropertyValue::MakeFloat(3.0f)) && sprite->size.x == 3.0f);

    // sub-field values are clamped like the whole
    CHECK(PropertyRegistry::SetProperty(*go, "SpriteRenderer.size.y", PropertyValue::MakeFloat(-5.0f)) && sprite->size.y == 0.0f);
    CHECK(PropertyRegistry::SetProperty(*go, "SpriteRenderer.color.a", PropertyValue::MakeFloat(7.0f)) && sprite->color.a == 1.0f);

    // text
    CHECK(PropertyRegistry::GetPropertyText(*go, "Light.type") == "Directional");
    CHECK(PropertyRegistry::SetPropertyFromText(*go, "Light.type", "spot") && light->type == Light::Type::Spot);
    CHECK(PropertyRegistry::GetPropertyText(*go, "Light.type") == "Spot");
    CHECK(PropertyRegistry::SetPropertyFromText(*go, "Transform.position", "(4, 5, 6)") && go->transform->localPosition.x == 4.0f);
    CHECK(PropertyRegistry::SetPropertyFromText(*go, "Transform.position.x", "7.5") && go->transform->localPosition.x == 7.5f);
    CHECK(PropertyRegistry::SetPropertyFromText(*go, "Light.color", "#FF0000") && light->color.r == 1.0f && light->color.g == 0.0f);
    CHECK(PropertyRegistry::GetPropertyText(*go, "Transform.position.x") == "7.5");
    CHECK(PropertyRegistry::GetPropertyText(*go, "Transform.position") == "7.5 5 6");

    // two components of one type
    Camera* cam2 = go->AddComponent<Camera>();
    cam2->depth = 5.0f;
    CHECK(PropertyRegistry::GetProperty(*go, "Camera.depth") == PropertyValue::MakeFloat(0.0f));
    CHECK(PropertyRegistry::GetProperty(*go, "Camera#0.depth") == PropertyValue::MakeFloat(0.0f));
    CHECK(PropertyRegistry::GetProperty(*go, "Camera#1.depth") == PropertyValue::MakeFloat(5.0f));
    CHECK(PropertyRegistry::SetProperty(*go, "Camera#1.depth", PropertyValue::MakeFloat(8.0f)) && cam2->depth == 8.0f && cam->depth == 0.0f);
    CHECK(!PropertyRegistry::GetProperty(*go, "Camera#2.depth").IsValid());

    // GameObject fields
    CHECK(PropertyRegistry::GetProperty(*go, "GameObject.name") == PropertyValue::MakeString("Thing"));
    CHECK(PropertyRegistry::SetProperty(*go, "GameObject.name", PropertyValue::MakeString("Renamed")) && go->name == "Renamed");
    CHECK(PropertyRegistry::SetProperty(*go, "GameObject.active", PropertyValue::MakeBool(false)) && !go->active);
    CHECK(PropertyRegistry::SetProperty(*go, "GameObject.layer", PropertyValue::MakeInt(40)) && go->layer == 31);
    CHECK(PropertyRegistry::SetProperty(*go, "GameObject.tag", PropertyValue::MakeString("Enemy")) && go->tag == "Enemy");
    CHECK(PropertyRegistry::GetProperty(*go, "gameobject.istatic").IsValid() == false);   // no such property
    CHECK(PropertyRegistry::GetProperty(*go, "GameObject.isStatic") == PropertyValue::MakeBool(false));

    // errors say why and change nothing
    const PropertySnapshot before = SnapshotProperties(*go);
    std::string err;
    PropertyValue v;
    CHECK(!PropertyRegistry::TryGetProperty(*go, "Light", v, &err) && !err.empty());
    CHECK(!PropertyRegistry::TryGetProperty(*go, ".intensity", v, &err) && !err.empty());
    CHECK(!PropertyRegistry::TryGetProperty(*go, "Light.", v, &err) && !err.empty());
    CHECK(!PropertyRegistry::TryGetProperty(*go, "Bogus.intensity", v, &err) && err.find("Bogus") != std::string::npos);
    CHECK(!PropertyRegistry::TryGetProperty(*go, "Light.bogus", v, &err) && err.find("bogus") != std::string::npos);
    CHECK(!PropertyRegistry::TryGetProperty(*go, "Light.intensity.x", v, &err) && !err.empty());
    CHECK(!PropertyRegistry::TryGetProperty(*go, "Light.color.x", v, &err) && !err.empty());
    CHECK(!PropertyRegistry::TryGetProperty(*go, "Transform.position.w", v, &err) && !err.empty());
    CHECK(!PropertyRegistry::TryGetProperty(*go, "Transform.position.x.y", v, &err) && !err.empty());
    CHECK(!PropertyRegistry::TryGetProperty(*go, "Camera#x.depth", v, &err) && !err.empty());
    CHECK(!PropertyRegistry::TryGetProperty(*go, "Camera#.depth", v, &err) && !err.empty());
    CHECK(!PropertyRegistry::TryGetProperty(*go, "Camera#-1.depth", v, &err) && !err.empty());
    GameObject* bare = s.CreateGameObject("Bare");
    CHECK(!PropertyRegistry::TryGetProperty(*bare, "Light.intensity", v, &err) && err.find("no Light") != std::string::npos);
    CHECK(!PropertyRegistry::SetProperty(*bare, "Light.intensity", PropertyValue::MakeFloat(1.0f), &err));
    CHECK(!PropertyRegistry::SetProperty(*go, "Light.intensity", PropertyValue::MakeBool(true), &err) && !err.empty());   // wrong type
    CHECK(!PropertyRegistry::SetProperty(*go, "Light.intensity", PropertyValue::MakeFloat(NAN), &err));
    CHECK(!PropertyRegistry::SetProperty(*go, "Light.color.r", PropertyValue::MakeVec3(Vec3{1, 1, 1}), &err));   // sub-field needs a Float
    CHECK(!PropertyRegistry::SetProperty(*go, "Light.type", PropertyValue::MakeEnum(9), &err));
    CHECK(!PropertyRegistry::SetProperty(*go, "Transform.rotation", PropertyValue::MakeQuat(Quat{0, 0, 0, 0}), &err));
    CHECK(!PropertyRegistry::SetProperty(*go, "GameObject.name", PropertyValue::MakeString("bad\nname"), &err));
    CHECK(!PropertyRegistry::SetPropertyFromText(*go, "Light.intensity", "abc", &err) && err.find("abc") != std::string::npos);
    CHECK(!PropertyRegistry::SetPropertyFromText(*go, "Transform.position", "1 2", &err));
    CHECK(!PropertyRegistry::SetPropertyFromText(*go, "Light.type", "Triangle", &err));
    CHECK(!PropertyRegistry::SetPropertyFromText(*go, "Transform.position.x", "x", &err));
    CHECK(DiffProperties(before, SnapshotProperties(*go)).empty());   // none of that changed anything

    // a read-only property cannot be set
    Rigidbody3D* rb = go->AddComponent<Rigidbody3D>();
    CHECK(!PropertyRegistry::SetProperty(*go, "Rigidbody3D.sleeping", PropertyValue::MakeBool(true), &err) && err.find("read-only") != std::string::npos);
    CHECK(!rb->sleeping);
    CHECK(PropertyRegistry::SetProperty(*go, "Rigidbody3D.velocity", PropertyValue::MakeVec3(Vec3{1, 2, 3})) && rb->velocity.y == 2.0f);

    // component-level access
    CHECK(PropertyRegistry::SetComponentProperty(*light, "range", PropertyValue::MakeFloat(33.0f)) && light->range == 33.0f);
    CHECK(PropertyRegistry::GetComponentProperty(*light, "range") == PropertyValue::MakeFloat(33.0f));
    CHECK(PropertyRegistry::SetComponentProperty(*light, "color.a", PropertyValue::MakeFloat(0.5f)) && light->color.a == 0.5f);
    CHECK(PropertyRegistry::GetComponentProperty(*light, "color.a") == PropertyValue::MakeFloat(0.5f));
    CHECK(!PropertyRegistry::SetComponentProperty(*light, "nope", PropertyValue::MakeFloat(1.0f)));
    CHECK(!PropertyRegistry::GetComponentProperty(*light, "nope").IsValid());
}

static void TestTransformProperties() {
    RUN_SUITE("Transform: rotation, euler, parent, world position");
    Scene s("s");
    GameObject* parent = s.CreateGameObject("Parent");
    GameObject* child = s.CreateGameObject("Child");
    parent->transform->localPosition = Vec3{10, 0, 0};

    // a non-unit rotation is normalised
    CHECK(PropertyRegistry::SetProperty(*child, "Transform.rotation", PropertyValue::MakeQuat(Quat{0, 0, 0, 3})));
    CHECK(child->transform->localRotation.w == 1.0f && child->transform->localRotation.x == 0.0f);

    // euler angles read and write the rotation
    CHECK(PropertyRegistry::SetProperty(*child, "Transform.eulerAngles", PropertyValue::MakeVec3(Vec3{0, 90, 0})));
    Vec3 e = PropertyRegistry::GetProperty(*child, "Transform.eulerAngles").AsVec3();
    CHECK_NEAR(e.y, 90.0f, 1e-3);
    CHECK_NEAR((child->transform->Forward() - Vec3{1, 0, 0}).Magnitude(), 0.0f, 1e-4);
    CHECK_NEAR(PropertyRegistry::GetProperty(*child, "Transform.rotation.y").AsFloat(), std::sqrt(0.5f), 1e-5);

    // parent by id keeps the local pose
    child->transform->localPosition = Vec3{1, 2, 3};
    CHECK(PropertyRegistry::GetProperty(*child, "Transform.parent") == PropertyValue::MakeObjectRef(""));
    CHECK(PropertyRegistry::SetProperty(*child, "Transform.parent", PropertyValue::MakeObjectRef(parent->Guid())));
    CHECK(child->transform->Parent() == parent->transform);
    CHECK(child->transform->localPosition.x == 1.0f && child->transform->localPosition.z == 3.0f);
    CHECK(PropertyRegistry::GetProperty(*child, "Transform.parent") == PropertyValue::MakeObjectRef(parent->Guid()));
    CHECK_NEAR(PropertyRegistry::GetProperty(*child, "Transform.worldPosition.x").AsFloat(), 11.0f, 1e-4);

    // world position writes through to the local one
    CHECK(PropertyRegistry::SetProperty(*child, "Transform.worldPosition", PropertyValue::MakeVec3(Vec3{14, 0, 0})));
    CHECK_NEAR(child->transform->localPosition.x, 4.0f, 1e-4);

    // refused: itself, a descendant (a loop), an unknown id
    std::string err;
    CHECK(!PropertyRegistry::SetProperty(*child, "Transform.parent", PropertyValue::MakeObjectRef(child->Guid()), &err));
    CHECK(!PropertyRegistry::SetProperty(*parent, "Transform.parent", PropertyValue::MakeObjectRef(child->Guid()), &err));
    CHECK(!PropertyRegistry::SetProperty(*child, "Transform.parent", PropertyValue::MakeObjectRef("0123456789abcdef"), &err));
    CHECK(child->transform->Parent() == parent->transform && parent->transform->Parent() == nullptr);
    // back to a root
    CHECK(PropertyRegistry::SetProperty(*child, "Transform.parent", PropertyValue::MakeObjectRef("")));
    CHECK(child->transform->Parent() == nullptr);
}

// ---- Change hook ---------------------------------------------------------------------------------

static void TestChangeHook() {
    RUN_SUITE("change hook");
    Scene s("s");
    GameObject* go = s.CreateGameObject("Thing");
    Light* light = go->AddComponent<Light>();
    Camera* cam0 = go->AddComponent<Camera>();
    Camera* cam1 = go->AddComponent<Camera>();
    (void)cam0;

    std::vector<PropertyChange> seen;
    const int id = PropertyRegistry::AddChangeHook([&](const PropertyChange& c) { seen.push_back(c); });
    CHECK(id > 0);

    CHECK(PropertyRegistry::SetProperty(*go, "Light.intensity", PropertyValue::MakeFloat(3.0f)));
    CHECK(seen.size() == 1);
    if (seen.size() == 1) {
        const PropertyChange& c = seen[0];
        CHECK(c.object == go && c.component == light);
        CHECK(c.componentInfo && c.componentInfo->name == "Light");
        CHECK(c.property && c.property->name == "intensity");
        CHECK(c.path == "Light.intensity");
        CHECK(c.componentIndex == 0);
        CHECK(c.oldValue == PropertyValue::MakeFloat(1.0f));
        CHECK(c.newValue == PropertyValue::MakeFloat(3.0f));
    }

    // setting the same value again is not a change
    seen.clear();
    CHECK(PropertyRegistry::SetProperty(*go, "Light.intensity", PropertyValue::MakeFloat(3.0f)));
    CHECK(seen.empty());

    // a clamped value reports what was stored
    seen.clear();
    CHECK(PropertyRegistry::SetProperty(*go, "Light.intensity", PropertyValue::MakeFloat(99999.0f)));
    CHECK(seen.size() == 1 && seen[0].newValue == PropertyValue::MakeFloat(1000.0f) && seen[0].oldValue == PropertyValue::MakeFloat(3.0f));
    // ...and a clamp onto the current value is no change either
    seen.clear();
    CHECK(PropertyRegistry::SetProperty(*go, "Light.intensity", PropertyValue::MakeFloat(12345.0f)));
    CHECK(seen.empty());

    // a sub-field change reports the whole property
    seen.clear();
    go->transform->localPosition = Vec3{1, 2, 3};
    CHECK(PropertyRegistry::SetProperty(*go, "Transform.position.y", PropertyValue::MakeFloat(8.0f)));
    CHECK(seen.size() == 1);
    if (seen.size() == 1) {
        CHECK(seen[0].path == "Transform.position");
        CHECK(seen[0].oldValue == PropertyValue::MakeVec3(Vec3{1, 2, 3}));
        CHECK(seen[0].newValue == PropertyValue::MakeVec3(Vec3{1, 8, 3}));
    }

    // an enum change carries the option names; the second camera has index 1
    seen.clear();
    CHECK(PropertyRegistry::SetProperty(*go, "Light.type", PropertyValue::MakeEnum(2)));
    CHECK(seen.size() == 1 && seen[0].oldValue.EnumName() == "Directional" && seen[0].newValue.EnumName() == "Spot");
    seen.clear();
    CHECK(PropertyRegistry::SetProperty(*go, "Camera#1.depth", PropertyValue::MakeFloat(4.0f)));
    CHECK(seen.size() == 1 && seen[0].path == "Camera#1.depth" && seen[0].componentIndex == 1 && seen[0].component == cam1);
    seen.clear();
    CHECK(PropertyRegistry::SetComponentProperty(*cam1, "depth", PropertyValue::MakeFloat(6.0f)));
    CHECK(seen.size() == 1 && seen[0].path == "Camera#1.depth");

    // refused sets fire nothing
    seen.clear();
    CHECK(!PropertyRegistry::SetProperty(*go, "Light.intensity", PropertyValue::MakeBool(true)));
    CHECK(!PropertyRegistry::SetProperty(*go, "Light.intensity", PropertyValue::MakeFloat(NAN)));
    CHECK(!PropertyRegistry::SetProperty(*go, "Light.nope", PropertyValue::MakeFloat(1.0f)));
    CHECK(!PropertyRegistry::SetProperty(*go, "Light.type", PropertyValue::MakeEnum(77)));
    CHECK(seen.empty());

    // suppression: the writes happen, the hook does not
    seen.clear();
    {
        PropertyRegistry::ScopedHookSuppression quiet;
        CHECK(PropertyRegistry::SetProperty(*go, "Light.range", PropertyValue::MakeFloat(77.0f)));
        {
            PropertyRegistry::ScopedHookSuppression nested;
            CHECK(PropertyRegistry::SetProperty(*go, "Light.range", PropertyValue::MakeFloat(78.0f)));
        }
        CHECK(PropertyRegistry::SetProperty(*go, "Light.range", PropertyValue::MakeFloat(79.0f)));
    }
    CHECK(seen.empty() && light->range == 79.0f);
    CHECK(PropertyRegistry::SetProperty(*go, "Light.range", PropertyValue::MakeFloat(11.0f)));
    CHECK(seen.size() == 1);

    // a second subscriber, a hook that sets another property, and removal
    int second = 0;
    const int id2 = PropertyRegistry::AddChangeHook([&](const PropertyChange&) { ++second; });
    seen.clear();
    CHECK(PropertyRegistry::SetProperty(*go, "Light.range", PropertyValue::MakeFloat(12.0f)));
    CHECK(seen.size() == 1 && second == 1);
    PropertyRegistry::RemoveChangeHook(id2);
    CHECK(PropertyRegistry::SetProperty(*go, "Light.range", PropertyValue::MakeFloat(13.0f)));
    CHECK(second == 1 && seen.size() == 2);

    bool cascaded = false;
    const int id3 = PropertyRegistry::AddChangeHook([&](const PropertyChange& c) {
        if (c.path == "Light.ambient" && !cascaded) { cascaded = true; PropertyRegistry::SetProperty(*go, "Light.spotAngle", PropertyValue::MakeFloat(60.0f)); }
    });
    seen.clear();
    CHECK(PropertyRegistry::SetProperty(*go, "Light.ambient", PropertyValue::MakeFloat(0.9f)));
    CHECK(cascaded && light->spotAngle == 60.0f);
    CHECK(seen.size() == 2 && seen[0].path == "Light.ambient" && seen[1].path == "Light.spotAngle");   // each change announced once
    PropertyRegistry::RemoveChangeHook(id3);

    // a hook that throws does not break the write
    const int id4 = PropertyRegistry::AddChangeHook([](const PropertyChange&) { throw 5; });
    CHECK(PropertyRegistry::SetProperty(*go, "Light.range", PropertyValue::MakeFloat(14.0f)) && light->range == 14.0f);
    PropertyRegistry::RemoveChangeHook(id4);

    // writing a derived property announces the stored property it changed
    seen.clear();
    const Quat q0 = go->transform->localRotation;
    CHECK(PropertyRegistry::SetProperty(*go, "Transform.eulerAngles", PropertyValue::MakeVec3(Vec3{0, 90, 0})));
    CHECK(seen.size() == 1);
    if (seen.size() == 1) {
        CHECK(seen[0].path == "Transform.rotation");
        CHECK(seen[0].oldValue == PropertyValue::MakeQuat(q0));
        CHECK(seen[0].newValue == PropertyValue::MakeQuat(go->transform->localRotation));
    }
    seen.clear();
    CHECK(PropertyRegistry::SetProperty(*go, "Transform.eulerAngles", PropertyValue::MakeVec3(Vec3{0, 90, 0})));   // same angles: no change
    CHECK(seen.empty());
    go->transform->localPosition = Vec3{0, 0, 0};
    CHECK(PropertyRegistry::SetProperty(*go, "Transform.worldPosition", PropertyValue::MakeVec3(Vec3{1, 2, 3})));
    CHECK(seen.size() == 1 && seen[0].path == "Transform.position" && seen[0].newValue == PropertyValue::MakeVec3(go->transform->localPosition));

    PropertyRegistry::RemoveChangeHook(id);
    seen.clear();
    CHECK(PropertyRegistry::SetProperty(*go, "Light.range", PropertyValue::MakeFloat(15.0f)));
    CHECK(seen.empty());
    CHECK(PropertyRegistry::AddChangeHook(nullptr) == 0);
}

// ---- Snapshot, diff, copy ---------------------------------------------------------------------------------

static void TestSnapshotDiffCopy() {
    RUN_SUITE("snapshot, diff, apply, copy");
    Scene s("s");
    GameObject* go = s.CreateGameObject("Thing");
    go->AddComponent<Light>();
    go->AddComponent<SpriteRenderer>();
    go->AddComponent<Rigidbody2D>();
    Camera* c0 = go->AddComponent<Camera>();
    Camera* c1 = go->AddComponent<Camera>();
    c1->depth = 2.0f;

    const PropertySnapshot a = SnapshotProperties(*go);
    CHECK(!a.Empty());
    CHECK(a.Find("Light.intensity") && a.Find("Light.intensity")->value == PropertyValue::MakeFloat(1.0f));
    CHECK(a.Find("Camera#1.depth") && a.Find("Camera#1.depth")->value == PropertyValue::MakeFloat(2.0f));
    CHECK(a.Find("Camera.depth") && a.Find("Camera#0.depth") == a.Find("Camera.depth"));
    CHECK(a.Find("GameObject.name") && a.Find("Transform.position"));
    CHECK(a.Find("Light.intensity")->Path() == "Light.intensity" && a.Find("Camera#1.depth")->Path() == "Camera#1.depth");
    // defaults leave out read-only, runtime-only and derived properties
    CHECK(!a.Find("Rigidbody2D.velocity") && !a.Find("Rigidbody2D.sleeping") && !a.Find("Transform.eulerAngles") && !a.Find("Transform.worldPosition"));
    CHECK(a.Find("Rigidbody2D.mass"));
    PropertySnapshotOptions all; all.includeReadOnly = all.includeRuntime = all.includeDerived = true;
    const PropertySnapshot everything = SnapshotProperties(*go, all);
    CHECK(everything.Find("Rigidbody2D.velocity") && everything.Find("Rigidbody2D.sleeping") && everything.Find("Transform.eulerAngles"));
    CHECK(everything.entries.size() > a.entries.size());
    PropertySnapshotOptions rt; rt.includeRuntime = true;
    CHECK(SnapshotProperties(*go, rt).Find("Rigidbody2D.velocity") && !SnapshotProperties(*go, rt).Find("Rigidbody2D.sleeping"));
    // an object with nothing but a Transform
    GameObject* bare = s.CreateGameObject("Bare");
    CHECK(SnapshotProperties(*bare).Find("Transform.position") && !SnapshotProperties(*bare).Find("Light.intensity"));
    CHECK(DiffProperties(a, a).empty());

    // change a handful of things
    PropertyRegistry::SetProperty(*go, "Light.intensity", PropertyValue::MakeFloat(4.0f));
    PropertyRegistry::SetProperty(*go, "Light.type", PropertyValue::MakeEnum(1));
    PropertyRegistry::SetProperty(*go, "Transform.position", PropertyValue::MakeVec3(Vec3{5, 6, 7}));
    PropertyRegistry::SetProperty(*go, "Camera#1.depth", PropertyValue::MakeFloat(9.0f));
    PropertyRegistry::SetProperty(*go, "SpriteRenderer.flipX", PropertyValue::MakeBool(true));
    PropertyRegistry::SetProperty(*go, "GameObject.name", PropertyValue::MakeString("Changed"));
    PropertyRegistry::SetProperty(*go, "Rigidbody2D.velocity", PropertyValue::MakeVec2(Vec2{1, 1}));   // runtime: not in the default snapshot

    const PropertySnapshot b = SnapshotProperties(*go);
    const std::vector<PropertyDiff> d = DiffProperties(a, b);
    std::set<std::string> paths;
    for (const auto& x : d) paths.insert(x.path);
    CHECK(d.size() == 6);
    CHECK((paths == std::set<std::string>{"Light.intensity", "Light.type", "Transform.position", "Camera#1.depth", "SpriteRenderer.flipX", "GameObject.name"}));
    for (const auto& x : d) {
        CHECK(x.kind == PropertyDiffKind::Changed);
        CHECK(x.before.IsValid() && x.after.IsValid() && x.before != x.after);
        CHECK(x.path == (x.index > 0 ? x.component + "#" + std::to_string(x.index) + "." + x.property : x.component + "." + x.property));
    }
    for (const auto& x : d) {
        if (x.path == "Light.intensity") CHECK(x.before == PropertyValue::MakeFloat(1.0f) && x.after == PropertyValue::MakeFloat(4.0f));
        if (x.path == "Transform.position") CHECK(x.after == PropertyValue::MakeVec3(Vec3{5, 6, 7}));
        if (x.path == "Light.type") CHECK(x.after.EnumName() == "Point");
    }

    // revert (per-property undo) and redo
    std::vector<PropertyChange> fired;
    const int hook = PropertyRegistry::AddChangeHook([&](const PropertyChange& c) { fired.push_back(c); });
    CHECK(ApplyPropertyDiff(*go, d, false) == 6);
    CHECK(fired.size() == 6);
    CHECK(DiffProperties(a, SnapshotProperties(*go)).empty());
    CHECK(go->name == "Thing" && c1->depth == 2.0f && c0->depth == 0.0f);
    CHECK(ApplyPropertyDiff(*go, d, true) == 6);
    CHECK(DiffProperties(b, SnapshotProperties(*go)).empty());
    // applying a snapshot restores everything it holds
    fired.clear();
    CHECK(ApplyPropertySnapshot(*go, a) > 0);
    CHECK(DiffProperties(a, SnapshotProperties(*go)).empty());
    CHECK(fired.size() == 6);   // only the six that actually differed announce themselves
    {   // quiet restore
        fired.clear();
        PropertyRegistry::ScopedHookSuppression quiet;
        ApplyPropertySnapshot(*go, b);
        CHECK(fired.empty());
    }
    CHECK(DiffProperties(b, SnapshotProperties(*go)).empty());
    PropertyRegistry::RemoveChangeHook(hook);

    // Added / Removed: compare an object with a Light against one without
    GameObject* other = s.CreateGameObject("Other");
    other->AddComponent<SpriteRenderer>();
    const std::vector<PropertyDiff> dd = DiffProperties(SnapshotProperties(*go), SnapshotProperties(*other));
    int removed = 0, added = 0, changed = 0;
    for (const auto& x : dd) {
        if (x.kind == PropertyDiffKind::Removed) { ++removed; CHECK(x.before.IsValid() && !x.after.IsValid()); }
        if (x.kind == PropertyDiffKind::Added) ++added;
        if (x.kind == PropertyDiffKind::Changed) ++changed;
    }
    CHECK(removed > 10 && added == 0 && changed > 0);
    const std::vector<PropertyDiff> ee = DiffProperties(SnapshotProperties(*other), SnapshotProperties(*go));
    int added2 = 0;
    for (const auto& x : ee) if (x.kind == PropertyDiffKind::Added) { ++added2; CHECK(!x.before.IsValid() && x.after.IsValid()); }
    CHECK(added2 == removed);
    // a diff never adds or removes components
    CHECK(ApplyPropertyDiff(*other, dd, true) <= changed);
    CHECK(other->GetComponent<Light>() == nullptr);

    // Copy
    GameObject* src = s.CreateGameObject("Src");
    src->AddComponent<Light>()->intensity = 7.0f;
    src->GetComponent<Light>()->color = Color{0.5f, 0.25f, 0.125f, 1.0f};
    src->AddComponent<SpriteRenderer>()->size = Vec2{3, 4};
    src->transform->localPosition = Vec3{9, 8, 7};
    src->transform->localScale = Vec3{2, 2, 2};
    GameObject* dst = s.CreateGameObject("Dst");
    dst->AddComponent<Light>();
    dst->AddComponent<Rigidbody2D>()->mass = 5.0f;
    dst->transform->SetParent(src->transform, false);

    fired.clear();
    const int hook2 = PropertyRegistry::AddChangeHook([&](const PropertyChange& c) { fired.push_back(c); });
    const int n = CopyProperties(*src, *dst);
    CHECK(n > 10);
    CHECK(dst->GetComponent<Light>()->intensity == 7.0f && dst->GetComponent<Light>()->color.g == 0.25f);
    CHECK(dst->transform->localPosition.x == 9.0f && dst->transform->localScale.y == 2.0f);
    CHECK(dst->GetComponent<SpriteRenderer>() == nullptr);            // not added by default
    CHECK(dst->name == "Dst" && dst->GetComponent<Rigidbody2D>()->mass == 5.0f);   // other fields untouched
    CHECK(dst->transform->Parent() == src->transform);                // hierarchy untouched
    CHECK(!fired.empty());
    bool announcedLight = false;
    for (const auto& c : fired) if (c.path == "Light.intensity" && c.newValue == PropertyValue::MakeFloat(7.0f)) announcedLight = true;
    CHECK(announcedLight);
    PropertyRegistry::RemoveChangeHook(hook2);

    PropertyCopyOptions opt;
    opt.includeTransform = false;
    opt.includeObjectFields = true;
    opt.addMissingComponents = true;
    dst->transform->localPosition = Vec3{0, 0, 0};
    CHECK(CopyProperties(*src, *dst, opt) > 10);
    CHECK(dst->transform->localPosition.x == 0.0f);                   // transform skipped
    CHECK(dst->name == "Src");                                        // object fields copied
    CHECK(dst->GetComponent<SpriteRenderer>() && dst->GetComponent<SpriteRenderer>()->size.x == 3.0f);
    opt = PropertyCopyOptions{};
    opt.onlyComponents = {"Light"};
    dst->GetComponent<Light>()->intensity = 1.0f;
    dst->GetComponent<SpriteRenderer>()->size = Vec2{1, 1};
    CHECK(CopyProperties(*src, *dst, opt) > 0);
    CHECK(dst->GetComponent<Light>()->intensity == 7.0f && dst->GetComponent<SpriteRenderer>()->size.x == 1.0f);
    // structural properties only when asked
    GameObject* par = s.CreateGameObject("Par");
    GameObject* kid = s.CreateGameObject("Kid");
    kid->transform->SetParent(par->transform, false);
    GameObject* loose = s.CreateGameObject("Loose");
    CopyProperties(*kid, *loose);
    CHECK(loose->transform->Parent() == nullptr);
    PropertyCopyOptions st; st.includeStructural = true;
    CopyProperties(*kid, *loose, st);
    CHECK(loose->transform->Parent() == par->transform);

    // component-level copy needs the same exact type
    Light* l1 = src->GetComponent<Light>();
    Light* l2 = dst->GetComponent<Light>();
    l2->intensity = 2.0f;
    CHECK(CopyComponentProperties(*l1, *l2) > 5 && l2->intensity == 7.0f);
    CHECK(CopyComponentProperties(*l1, *src->GetComponent<SpriteRenderer>()) == 0);
}

// ---- Every property of every built-in component ---------------------------------------------------------

// A value of the property's type that differs from `cur`, passes the descriptor
// unchanged (no clamping) and survives the scene file's six digits.
static bool CandidateOk(const PropertyDesc& d, const PropertyValue& cand, const PropertyValue& cur, PropertyValue& out) {
    PropertyValue n;
    if (!d.Normalize(cand, n)) return false;
    if (n != cand || n == cur) return false;
    out = n;
    return true;
}

static PropertyValue MakeDifferent(const PropertyDesc& d, const PropertyValue& cur, const std::string& otherGuid, bool rgbOnly) {
    PropertyValue out;
    switch (d.type) {
        case PropertyType::Bool: return PropertyValue::MakeBool(!cur.AsBool());
        case PropertyType::Int:
            for (long long c : {7LL, 3LL, 5LL, 1LL, 2LL, 16LL, 65LL, 100LL, -2LL, 12345LL})
                if (CandidateOk(d, PropertyValue::MakeInt(c), cur, out)) return out;
            break;
        case PropertyType::Float: {
            std::vector<float> cands = {0.75f, 2.5f, 12.5f, 0.25f, 0.125f, 100.5f, 3.0f, 0.5f};
            if (d.hasRange) {
                cands.push_back((float)(d.rangeMin + (d.rangeMax - d.rangeMin) * 0.25));
                cands.push_back((float)(d.rangeMin + (d.rangeMax - d.rangeMin) * 0.75));
            }
            for (float c : cands) if (CandidateOk(d, PropertyValue::MakeFloat(c), cur, out)) return out;
            break;
        }
        case PropertyType::String: return PropertyValue::MakeString("Test value 7");
        case PropertyType::AssetPath: return PropertyValue::MakeAssetPath("assets/test/file 7.dat");
        case PropertyType::ObjectRef: return PropertyValue::MakeObjectRef(otherGuid);
        case PropertyType::Enum: {
            const int n = (int)d.enumOptions.size();
            if (n < 2) break;
            return PropertyValue::MakeEnum((cur.AsEnum() + 1) % n);
        }
        case PropertyType::Quat: {
            for (Quat q : {Quat{0.5f, 0.5f, 0.5f, 0.5f}, Quat{-0.5f, 0.5f, 0.5f, 0.5f}})
                if (CandidateOk(d, PropertyValue::MakeQuat(q), cur, out)) return out;
            break;
        }
        default: {   // Vec2 / Vec3 / Vec4 / Color
            const float sets[][4] = {{0.75f, 1.5f, 2.25f, 0.5f}, {2.5f, 1.25f, 0.25f, 4.0f}, {0.125f, 0.375f, 0.625f, 0.875f}, {0.25f, 0.5f, 0.75f, 0.5f}};
            for (const auto& f : sets) {
                PropertyValue cand;
                switch (d.type) {
                    case PropertyType::Vec2: cand = PropertyValue::MakeVec2(Vec2{f[0], f[1]}); break;
                    case PropertyType::Vec3: cand = PropertyValue::MakeVec3(Vec3{f[0], f[1], f[2]}); break;
                    case PropertyType::Vec4: cand = PropertyValue::MakeVec4(Vec4{f[0], f[1], f[2], f[3]}); break;
                    default: cand = PropertyValue::MakeColor(Color{f[0], f[1], f[2], rgbOnly ? 1.0f : f[3]}); break;
                }
                if (CandidateOk(d, cand, cur, out)) return out;
            }
            break;
        }
    }
    return PropertyValue();
}

static bool Authored(const PropertyDesc& d) {
    return !d.IsReadOnly() && !d.IsRuntimeOnly() && !d.IsDerived();
}

// Build one object holding the component `info` describes (or the object itself
// for the GameObject entry).
static GameObject* MakeSubject(Scene& s, const ComponentTypeInfo& info) {
    GameObject* go = s.CreateGameObject("Subject");
    if (!info.objectLevel && info.add) info.add(*go);
    return go;
}

static void TestEveryPropertyRoundTrip() {
    RUN_SUITE("every built-in property: get -> set -> get, text, change");
    RegisterBuiltinProperties();
    int props = 0, comps = 0;
    for (const auto& info : PropertyRegistry::All()) {
        ++comps;
        Scene s("s");
        GameObject* other = s.CreateGameObject("Other");
        GameObject* go = MakeSubject(s, *info);
        const std::string base = info->name + ".";
        CHECK(!info->properties.empty());
        for (const PropertyDesc& d : info->properties) {
            ++props;
            const std::string path = base + d.name;
            CHECK(!d.name.empty() && d.type != PropertyType::None && d.getter && d.setter);
            // no two properties share a name
            int dup = 0; for (const auto& o : info->properties) if (o.name == d.name) ++dup;
            CHECK(dup == 1);
            if (d.type == PropertyType::Enum) CHECK(!d.enumOptions.empty());

            const PropertyValue v0 = PropertyRegistry::GetProperty(*go, path);
            CHECK(v0.IsValid() && v0.Type() == d.type);
            if (!v0.IsValid()) continue;
            if (d.defaultValue.IsValid()) CHECK(d.defaultValue == v0);   // freshly made = the default

            // text round trip, whole value and friendly form
            CHECK(RoundTrips(v0));
            PropertyValue parsed;
            CHECK(d.ParseText(d.FormatText(v0), parsed) && parsed == v0);

            if (d.IsReadOnly()) {
                CHECK(!PropertyRegistry::SetProperty(*go, path, v0));
                continue;
            }
            // get -> set -> get leaves the value alone and fires nothing
            int fires = 0;
            const int hook = PropertyRegistry::AddChangeHook([&](const PropertyChange&) { ++fires; });
            CHECK(PropertyRegistry::SetProperty(*go, path, v0));
            CHECK(PropertyRegistry::GetProperty(*go, path) == v0);
            CHECK(fires == 0);

            // a different value goes in, comes out, and fires once with old and new
            const PropertyValue v1 = MakeDifferent(d, v0, other->Guid(), false);
            CHECK(v1.IsValid());
            // A derived property is computed from others (euler angles from a quaternion):
            // it reads back within float error rather than bit for bit.
            auto same = [&](const PropertyValue& a, const PropertyValue& b) { return d.IsDerived() ? Close(a, b) : a == b; };
            if (v1.IsValid()) {
                CHECK(PropertyRegistry::SetProperty(*go, path, v1));
                const PropertyValue got = PropertyRegistry::GetProperty(*go, path);
                if (!same(got, v1)) std::cerr << "    " << path << ": set '" << v1.ToString() << "' got '" << got.ToString() << "'\n";
                CHECK(same(got, v1));
                CHECK(fires == 1);
                // by text
                CHECK(PropertyRegistry::SetPropertyFromText(*go, path, v0.ToString()));
                CHECK(same(PropertyRegistry::GetProperty(*go, path), v0));
                CHECK(PropertyRegistry::SetPropertyFromText(*go, path, got.ToString()));
                CHECK(same(PropertyRegistry::GetProperty(*go, path), got));
                CHECK(PropertyRegistry::SetProperty(*go, path, v0));   // back to the start
                CHECK(same(PropertyRegistry::GetProperty(*go, path), v0));
            }
            // the wrong type is always refused
            const PropertyValue wrong = d.type == PropertyType::Bool ? PropertyValue::MakeFloat(1.0f) : PropertyValue::MakeBool(true);
            const int firesBefore = fires;
            CHECK(!PropertyRegistry::SetProperty(*go, path, wrong));
            CHECK(same(PropertyRegistry::GetProperty(*go, path), v0) && fires == firesBefore);
            PropertyRegistry::RemoveChangeHook(hook);
        }
    }
    std::cout << "  covered " << comps << " component types, " << props << " properties\n";
    CHECK(comps >= 26 && props >= 300);
}

// ---- Cross-check against the scene serializer ---------------------------------------------------------------

// Properties the scene serializer does not keep. Everything else with the
// default flags (not read-only, runtime-only or derived) must survive Serialize
// and Deserialize. Empty on purpose: every registered authored property is stored.
static const std::set<std::string> kNotStored = {};
// The serializer keeps these colors as red, green and blue only (alpha stays 1).
static const std::set<std::string> kRgbOnly = {
    "Light.ambientColor", "MeshRenderer.emissive", "MeshRenderer.gradientTop", "MeshRenderer.gradientBottom",
    "MeshRenderer.rimColor", "MeshRenderer.outlineColor",
};

static void TestSerializerCrossCheck() {
    RUN_SUITE("cross-check against the scene serializer");
    RegisterBuiltinProperties();
    int checked = 0, skippedFlags = 0, skippedList = 0;
    std::vector<std::string> coveredComponents;
    for (const auto& info : PropertyRegistry::All()) {
        Scene s1("s1");
        GameObject* other = s1.CreateGameObject("Other");
        GameObject* go = MakeSubject(s1, *info);
        std::map<std::string, PropertyValue> expect;
        std::vector<std::string> storedNames;
        for (const PropertyDesc& d : info->properties) {
            const std::string key = info->name + "." + d.name;
            if (!Authored(d)) { ++skippedFlags; continue; }
            if (kNotStored.count(key)) { ++skippedList; continue; }
            const PropertyValue cur = PropertyRegistry::GetProperty(*go, key);
            const PropertyValue v = MakeDifferent(d, cur, other->Guid(), kRgbOnly.count(key) != 0);
            CHECK(v.IsValid());
            if (!v.IsValid()) continue;
            std::string err;
            CHECK(PropertyRegistry::SetProperty(*go, key, v, &err));
            if (!err.empty()) std::cerr << "    " << key << ": " << err << "\n";
            expect[key] = PropertyRegistry::GetProperty(*go, key);
        }
        const std::string text = SceneSerializer::Serialize(s1);
        if (!info->recordName.empty() && !info->objectLevel)
            CHECK(text.find("\n  " + info->recordName + " ") != std::string::npos || text.find("\n  " + info->recordName + "\n") != std::string::npos);
        Scene s2("s2");
        std::string err;
        CHECK(SceneSerializer::Deserialize(s2, text, &err));
        GameObject* back = s2.FindByGuid(go->Guid());
        CHECK(back != nullptr);
        if (!back) continue;
        int bad = 0;
        for (const auto& kv : expect) {
            const PropertyValue got = PropertyRegistry::GetProperty(*back, kv.first);
            ++checked;
            if (!Close(got, kv.second)) {
                ++bad;
                std::cerr << "  MISMATCH " << kv.first << ": set '" << kv.second.ToString() << "' read back '" << got.ToString() << "'\n";
            }
        }
        CHECK(bad == 0);
        coveredComponents.push_back(info->name + "(" + std::to_string(expect.size()) + "/" + std::to_string(info->properties.size()) + ")");
    }
    std::cout << "  cross-checked " << checked << " properties; skipped " << skippedFlags
              << " read-only/runtime/derived and " << skippedList << " listed; components:";
    for (const auto& c : coveredComponents) std::cout << " " << c;
    std::cout << "\n";
    CHECK(checked >= 250);
}

static void TestSerializerCrossCheckIsolated() {
    RUN_SUITE("cross-check, one property at a time (shows what the file stores unconditionally)");
    RegisterBuiltinProperties();
    // The scene file writes these only together with another field (see
    // SceneSerializer.cpp: the shader record needs a non-Standard shader, the rim
    // record a rim strength, ...), so alone they would be lost: each rides on its companion.
    const std::map<std::string, std::pair<std::string, PropertyValue>> companion = {
        {"MeshRenderer.toonBands", {"MeshRenderer.shader", PropertyValue::MakeEnum(2)}},
        {"MeshRenderer.gradientTop", {"MeshRenderer.shader", PropertyValue::MakeEnum(2)}},
        {"MeshRenderer.gradientBottom", {"MeshRenderer.shader", PropertyValue::MakeEnum(2)}},
        {"MeshRenderer.rimPower", {"MeshRenderer.rimStrength", PropertyValue::MakeFloat(0.5f)}},
        {"MeshRenderer.rimColor", {"MeshRenderer.rimStrength", PropertyValue::MakeFloat(0.5f)}},
        {"MeshRenderer.outlineColor", {"MeshRenderer.outline", PropertyValue::MakeBool(true)}},
        {"MeshRenderer.outlineWidth", {"MeshRenderer.outline", PropertyValue::MakeBool(true)}},
        {"MeshRenderer.normalStrength", {"MeshRenderer.normalMap", PropertyValue::MakeAssetPath("n.png")}},
        {"MeshRenderer.aoStrength", {"MeshRenderer.aoMap", PropertyValue::MakeAssetPath("ao.png")}},
    };
    int checked = 0, viaCompanion = 0;
    for (const auto& info : PropertyRegistry::All()) {
        for (const PropertyDesc& d : info->properties) {
            if (!Authored(d)) continue;
            const std::string key = info->name + "." + d.name;
            Scene s1("s1");
            GameObject* other = s1.CreateGameObject("Other");
            GameObject* go = MakeSubject(s1, *info);
            auto comp = companion.find(key);
            if (comp != companion.end()) {
                CHECK(PropertyRegistry::SetProperty(*go, comp->second.first, comp->second.second));
                ++viaCompanion;
            }
            const PropertyValue cur = PropertyRegistry::GetProperty(*go, key);
            const PropertyValue v = MakeDifferent(d, cur, other->Guid(), kRgbOnly.count(key) != 0);
            if (!v.IsValid()) { CHECK(v.IsValid()); continue; }
            CHECK(PropertyRegistry::SetProperty(*go, key, v));
            const PropertyValue want = PropertyRegistry::GetProperty(*go, key);
            Scene s2("s2");
            CHECK(SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s1)));
            GameObject* back = s2.FindByGuid(go->Guid());
            CHECK(back != nullptr);
            if (!back) continue;
            ++checked;
            const PropertyValue got = PropertyRegistry::GetProperty(*back, key);
            if (!Close(got, want)) std::cerr << "  MISMATCH (alone) " << key << ": set '" << want.ToString() << "' read back '" << got.ToString() << "'\n";
            CHECK(Close(got, want));
        }
    }
    std::cout << "  " << checked << " properties checked alone, " << viaCompanion << " with a companion field\n";
}

// ---- A third-party component ------------------------------------------------------------------------------

namespace {
class TestWidget : public Component {
public:
    enum class Mode { Idle, Run, Jump };
    float speed = 1.0f;
    int count = 2;
    bool armed = false;
    std::string label = "widget";
    Vec3 dir{0, 1, 0};
    Color tint{1, 0, 0, 1};
    Mode mode = Mode::Idle;
    std::string clip;
    float strict = 5.0f;
    float computed() const { return speed * 2.0f; }
    int hidden = 3;
    int stat = 0;
    Vec2 size{1, 1};
    Quat spin;
};
}

static void TestCustomComponent() {
    RUN_SUITE("third-party component through PropertyBuilder");
    const std::size_t before = PropertyRegistry::All().size();
    PropertyBuilder<TestWidget> b("TestWidget", "testwidget", "Test");
    b.Section("Motion")
     .AddFloat("speed", &TestWidget::speed).Range(0, 10, 0.5).Tooltip("Units per second")
     .AddInt("count", &TestWidget::count).Range(0, 5)
     .Section("")
     .AddBool("armed", &TestWidget::armed)
     .AddString("label", &TestWidget::label)
     .AddVec3("dir", &TestWidget::dir)
     .AddColor("tint", &TestWidget::tint)
     .AddEnum("mode", &TestWidget::mode, {"Idle", "Run", "Jump"})
     .AddAssetPath("clip", &TestWidget::clip, "audio")
     .AddFloat("strict", &TestWidget::strict).Range(0, 10).RejectOutOfRange()
     .Custom("doubleSpeed", PropertyType::Float,
             [](const TestWidget& w) { return PropertyValue::MakeFloat(w.computed()); },
             [](TestWidget& w, const PropertyValue& v) { w.speed = v.AsFloat() / 2.0f; return true; }).Derived()
     .AddInt("hidden", &TestWidget::hidden).Hidden().Group("Internal")
     .AddInt("stat", &TestWidget::stat).ReadOnly()
     .AddVec2("size", &TestWidget::size).Range(0.5, 4)
     .AddQuat("spin", &TestWidget::spin);
    b.Commit();
    CHECK(PropertyRegistry::All().size() == before + 1);

    auto info = PropertyRegistry::FindByName("testwidget");
    CHECK(info && info->name == "TestWidget" && info->category == "Test");
    CHECK(info->Find("enabled") != nullptr);                       // every component gets one
    CHECK(info->Find("speed")->group == "Motion" && info->Find("speed")->tooltip == "Units per second" && info->Find("speed")->step == 0.5);
    CHECK(info->Find("armed")->group.empty());
    CHECK(info->Find("clip")->assetKind == "audio" && info->Find("clip")->type == PropertyType::AssetPath);
    CHECK(info->Find("hidden")->IsHidden() && info->Find("hidden")->group == "Internal");
    CHECK(info->Find("stat")->IsReadOnly() && info->Find("doubleSpeed")->IsDerived());
    CHECK(info->Find("speed")->defaultValue == PropertyValue::MakeFloat(1.0f));
    CHECK(info->Find("tint")->defaultValue == PropertyValue::MakeColor(Color{1, 0, 0, 1}));
    CHECK(info->Find("mode")->enumOptions.size() == 3);
    // the field type's own limits are the starting range of an integer
    CHECK(info->Find("hidden")->hasRange && info->Find("hidden")->rangeMin == (double)INT_MIN);

    Scene s("s");
    GameObject* go = s.CreateGameObject("W");
    TestWidget* w = go->AddComponent<TestWidget>();
    CHECK(PropertyRegistry::FindFor(*w) == info);
    CHECK(PropertyRegistry::SetProperty(*go, "TestWidget.speed", PropertyValue::MakeFloat(4.0f)) && w->speed == 4.0f);
    CHECK(PropertyRegistry::SetProperty(*go, "TestWidget.speed", PropertyValue::MakeFloat(40.0f)) && w->speed == 10.0f);     // clamped
    CHECK(PropertyRegistry::SetProperty(*go, "TestWidget.count", PropertyValue::MakeInt(9)) && w->count == 5);
    CHECK(PropertyRegistry::SetProperty(*go, "TestWidget.mode", PropertyValue::MakeEnum(2)) && w->mode == TestWidget::Mode::Jump);
    CHECK(PropertyRegistry::GetPropertyText(*go, "testwidget.mode") == "Jump");
    CHECK(PropertyRegistry::SetPropertyFromText(*go, "TestWidget.mode", "run") && w->mode == TestWidget::Mode::Run);
    CHECK(PropertyRegistry::SetProperty(*go, "TestWidget.label", PropertyValue::MakeString("hi there")) && w->label == "hi there");
    CHECK(!PropertyRegistry::SetProperty(*go, "TestWidget.label", PropertyValue::MakeString("two\nlines")) && w->label == "hi there");
    CHECK(PropertyRegistry::SetProperty(*go, "TestWidget.dir.z", PropertyValue::MakeFloat(2.0f)) && w->dir.z == 2.0f && w->dir.y == 1.0f);
    CHECK(PropertyRegistry::SetProperty(*go, "TestWidget.tint.a", PropertyValue::MakeFloat(0.5f)) && w->tint.a == 0.5f);
    CHECK(PropertyRegistry::SetProperty(*go, "TestWidget.enabled", PropertyValue::MakeBool(false)) && !w->enabled);
    CHECK(PropertyRegistry::SetProperty(*go, "TestWidget.size", PropertyValue::MakeVec2(Vec2{0.1f, 9})) && w->size.x == 0.5f && w->size.y == 4.0f);
    CHECK(PropertyRegistry::SetProperty(*go, "TestWidget.doubleSpeed", PropertyValue::MakeFloat(6.0f)) && w->speed == 3.0f);
    CHECK(PropertyRegistry::GetProperty(*go, "TestWidget.doubleSpeed") == PropertyValue::MakeFloat(6.0f));

    // reject mode
    w->strict = 5.0f;
    CHECK(!PropertyRegistry::SetProperty(*go, "TestWidget.strict", PropertyValue::MakeFloat(11.0f)) && w->strict == 5.0f);
    CHECK(!PropertyRegistry::SetProperty(*go, "TestWidget.strict", PropertyValue::MakeFloat(-1.0f)) && w->strict == 5.0f);
    CHECK(PropertyRegistry::SetProperty(*go, "TestWidget.strict", PropertyValue::MakeFloat(10.0f)) && w->strict == 10.0f);
    // read-only
    CHECK(!PropertyRegistry::SetProperty(*go, "TestWidget.stat", PropertyValue::MakeInt(1)) && w->stat == 0);

    // it takes part in snapshots, diffs and copies; derived and read-only stay out
    const PropertySnapshot snap = SnapshotProperties(*go);
    CHECK(snap.Find("TestWidget.speed") && !snap.Find("TestWidget.doubleSpeed") && !snap.Find("TestWidget.stat") && snap.Find("TestWidget.hidden"));
    GameObject* go2 = s.CreateGameObject("W2");
    TestWidget* w2 = go2->AddComponent<TestWidget>();
    CopyProperties(*go, *go2);
    CHECK(w2->speed == w->speed && w2->mode == w->mode && w2->label == "hi there" && !w2->enabled);

    // and the registry can add one by itself
    GameObject* go3 = s.CreateGameObject("W3");
    CHECK(info->add && info->add(*go3) && go3->GetComponent<TestWidget>() != nullptr);

    // re-registering replaces (no duplicates)
    PropertyBuilder<TestWidget> again("TestWidget", "testwidget", "Test2");
    again.AddFloat("speed", &TestWidget::speed);
    again.Commit();
    CHECK(PropertyRegistry::All().size() == before + 1);
    CHECK(PropertyRegistry::FindByName("TestWidget")->category == "Test2" && PropertyRegistry::FindByName("TestWidget")->properties.size() == 2);   // enabled + speed
    CHECK(info->properties.size() > 2);   // the old description stays valid for whoever holds it

    // a registration without a name or with unusable properties
    ComponentTypeInfo bad;
    PropertyRegistry::Register(bad);
    CHECK(PropertyRegistry::All().size() == before + 1);
    ComponentTypeInfo half;
    half.name = "Half";
    half.type = std::type_index(typeid(int));
    PropertyDesc nameless; nameless.type = PropertyType::Float; nameless.getter = [](const Component&) { return PropertyValue::MakeFloat(1.0f); };
    PropertyDesc typeless; typeless.name = "t"; typeless.getter = nameless.getter;
    PropertyDesc getterless; getterless.name = "g"; getterless.type = PropertyType::Float;
    half.properties = {nameless, typeless, getterless};
    PropertyRegistry::Register(half);
    CHECK(PropertyRegistry::FindByName("Half") && PropertyRegistry::FindByName("Half")->properties.empty());

    // entries with no type set never replace each other
    ComponentTypeInfo noTypeA, noTypeB;
    noTypeA.name = "NoTypeA";
    noTypeB.name = "NoTypeB";
    PropertyRegistry::Register(noTypeA);
    PropertyRegistry::Register(noTypeB);
    CHECK(PropertyRegistry::FindByName("NoTypeA") && PropertyRegistry::FindByName("NoTypeB"));
    CHECK(PropertyRegistry::FindByType(typeid(void)) == nullptr);
}

int main() {
    TestValueRoundTrip();
    TestValueBadInput();
    TestDescriptor();
    TestRegistryBasics();
    TestPaths();
    TestTransformProperties();
    TestChangeHook();
    TestSnapshotDiffCopy();
    TestEveryPropertyRoundTrip();
    TestSerializerCrossCheck();
    TestSerializerCrossCheckIsolated();
    TestCustomComponent();
    TestThreadSafeRegistration();
    TEST_MAIN_RESULT();
}
