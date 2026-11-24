// The generic script binding layer (okay/Scripting/ScriptBindings.hpp): a script holds any engine component as a
// handle, calls its generated methods and reads and sets its registry properties. Written before the layer was wired
// in: method calls, properties, clear errors for a wrong type / a missing member / a null component, handles to
// destroyed objects reading as null, and a parity walk over every bound class and method.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Camera.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/LineRenderer.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scripting/ScriptBindings.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace okay;
namespace sb = okay::scriptbind;

namespace {

// Test functions start with t_ so none shares a name with a builtin (a builtin wins a call by name).
// A scene with a "Lamp" (Camera + Light + Rigidbody) running `src`; Run calls one of its functions.
struct Rig {
    Scene scene{"Bind"};
    GameObject* lamp = nullptr;
    GameObject* other = nullptr;
    Camera* cam = nullptr;
    Light* light = nullptr;
    Rigidbody3D* rb = nullptr;
    ScriptComponent* sc = nullptr;
    bool loaded = false;
    explicit Rig(const std::string& src) {
        lamp = scene.CreateGameObject("Lamp");
        cam = lamp->AddComponent<Camera>();
        light = lamp->AddComponent<Light>();
        rb = lamp->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        other = scene.CreateGameObject("Other");
        other->AddComponent<Light>()->intensity = 7.0f;
        sc = lamp->AddComponent<ScriptComponent>("okayscript");
        std::string e;
        loaded = sc->LoadSource(src, &e);
        if (!loaded) std::printf("  load error: %s\n", e.c_str());
        scene.Start();
    }
    vs::VsValue Run(const char* fn, std::string* err = nullptr) {
        std::string e;
        vs::VsValue v = sc->VM()->CallEventArgs(fn, {}, &e);
        if (err) *err = e;
        else if (!e.empty()) std::printf("  %s(): %s\n", fn, e.c_str());
        return v;
    }
};

// A sample script value for an argument kind.
vs::VsValue Sample(sb::ArgKind k, GameObject* self) {
    switch (k) {
        case sb::ArgKind::Bool: return vs::VsValue{true};
        case sb::ArgKind::Int: case sb::ArgKind::Enum: return vs::VsValue{0.0f};
        case sb::ArgKind::Float: return vs::VsValue{1.0f};
        case sb::ArgKind::String: return vs::VsValue{std::string("x")};
        case sb::ArgKind::Color: return vs::VsValue::Color(1, 1, 1, 1);
        case sb::ArgKind::Object: return vs::VsValue::Object(self->InstanceId());
        default: return vs::VsValue{Vec3{1, 1, 1}};
    }
}

bool Has(const std::string& s, const std::string& part) {
    const bool ok = s.find(part) != std::string::npos;
    if (!ok) std::printf("  expected \"%s\" in \"%s\"\n", part.c_str(), s.c_str());
    return ok;
}

} // namespace

int main() {
    RUN_SUITE("script_bindings");

    // ---- Properties through the registry ----
    {
        Rig r("function t_setRead() { var l = GetComponent(\"Light\"); l.intensity = 2.5; return l.intensity; }\n"
              "function t_direct() { GetComponent(\"Light\").intensity = 4; return GetComponent(\"Light\").intensity; }\n"
              "var held = null;\n"
              "function t_grab() { held = GetComponent(\"Light\"); return held != null; }\n"
              "function t_viaGlobal() { held.range = 12; return held.range; }\n"
              "function t_enumSet() { var l = GetComponent(\"Light\"); l.type = \"Spot\"; return l.type; }\n"
              "function t_camFov() { var c = GetComponent(\"Camera\"); c.fieldOfView = 50; return c.fieldOfView; }\n"
              "function t_clamped() { var c = GetComponent(\"Camera\"); c.fieldOfView = 500; return c.fieldOfView; }\n"
              "function t_enabledFlag() { var l = GetComponent(\"Light\"); l.enabled = false; return l.enabled; }\n"
              "function t_ownerName() { return GetComponent(\"Light\").gameObject.name; }\n");
        CHECK(r.loaded);
        CHECK_NEAR(r.Run("t_setRead").AsFloat(), 2.5f, 1e-5f);
        CHECK_NEAR(r.light->intensity, 2.5f, 1e-5f);
        CHECK_NEAR(r.Run("t_direct").AsFloat(), 4.0f, 1e-5f);
        CHECK_NEAR(r.light->intensity, 4.0f, 1e-5f);
        CHECK(r.Run("t_grab").AsBool());
        CHECK_NEAR(r.Run("t_viaGlobal").AsFloat(), 12.0f, 1e-5f);
        CHECK_NEAR(r.light->range, 12.0f, 1e-5f);
        CHECK(r.Run("t_enumSet").AsString() == "Spot");
        CHECK(r.light->type == Light::Type::Spot);
        CHECK_NEAR(r.Run("t_camFov").AsFloat(), 50.0f, 1e-4f);
        CHECK_NEAR(r.cam->fieldOfView, 50.0f, 1e-4f);
        CHECK_NEAR(r.Run("t_clamped").AsFloat(), 179.0f, 1e-4f);   // the registry's range, as the Inspector clamps it
        CHECK(!r.Run("t_enabledFlag").AsBool());
        CHECK(!r.light->enabled);
        CHECK(r.Run("t_ownerName").AsString() == "Lamp");
    }

    // ---- Generated methods ----
    {
        Rig r("function t_shake() { var c = GetComponent(\"Camera\"); c.Shake(0.8); return c.IsShaking(); }\n"
              "function t_trauma() { return GetComponent(\"Camera\").ShakeTrauma(); }\n"
              "function t_defaults() { var c = GetComponent(\"Camera\"); c.Shake(); return c.ShakeTrauma(); }\n"
              "function t_invMass() { var rb = GetComponent(\"Rigidbody\"); rb.mass = 4; return rb.InvMass(); }\n"
              "function t_otherLight() { return GameObject.Find(\"Other\").GetComponent(\"Light\").intensity; }\n"
              "function t_rbProp() { var rb = GetComponent(\"Rigidbody\"); rb.continuousCollision = true; return rb.continuousCollision; }\n");
        CHECK(r.loaded);
        CHECK_NEAR(r.Run("t_defaults").AsFloat(), 0.5f, 1e-5f);   // Shake(strength = 0.5): the C++ default filled in
        CHECK(r.Run("t_shake").AsBool());
        CHECK(r.cam->IsShaking());
        CHECK(r.Run("t_trauma").AsFloat() > 0.0f);
        CHECK_NEAR(r.Run("t_invMass").AsFloat(), 0.25f, 1e-5f);   // the hand-written Rigidbody ref falls through to the generated InvMass
        CHECK_NEAR(r.Run("t_otherLight").AsFloat(), 7.0f, 1e-5f);
        CHECK(r.Run("t_rbProp").AsBool());
        CHECK(r.rb->continuousCollision);
    }

    // ---- Clear errors ----
    {
        Rig r("function t_badType() { GetComponent(\"Light\").intensity = \"bright\"; }\n"
              "function t_badArg() { GetComponent(\"Camera\").KickFov(\"lots\"); }\n"
              "function t_badCount() { GetComponent(\"Camera\").ZoomTo(); }\n"
              "function t_noMethod() { GetComponent(\"Camera\").Fly(); }\n"
              "function t_noProp() { return GetComponent(\"Light\").brightness; }\n"
              "function t_noSetProp() { var l = GetComponent(\"Light\"); l.brightness = 1; }\n"
              "function t_missing() { return GetComponent(\"NavMeshAgent\") == null; }\n"
              "function t_missingCall() { var a = GetComponent(\"NavMeshAgent\"); a.Stop(); }\n"
              "function t_badOption() { GetComponent(\"Light\").type = \"Laser\"; }\n");
        CHECK(r.loaded);
        std::string e;
        r.Run("t_badType", &e);
        CHECK(Has(e, "Light.intensity wants a number, got text"));
        CHECK(e.rfind("line 1: ", 0) == 0);   // the error names the script line it happened on
        CHECK_NEAR(r.light->intensity, Light{}.intensity, 1e-6f);   // untouched
        r.Run("t_badArg", &e);
        CHECK(Has(e, "Camera.KickFov(): argument 1 must be a number, got text"));
        CHECK(e.rfind("line 2: ", 0) == 0);
        r.Run("t_badCount", &e);
        CHECK(Has(e, "Camera.ZoomTo() takes 2 arguments, got 0"));
        r.Run("t_noMethod", &e);
        CHECK(Has(e, "Camera has no method 'Fly'"));
        r.Run("t_noProp", &e);
        CHECK(Has(e, "Light has no property 'brightness'"));
        r.Run("t_noSetProp", &e);
        CHECK(Has(e, "Light has no such property"));
        CHECK(r.Run("t_missing").AsBool());
        r.Run("t_missingCall", &e);
        CHECK(!e.empty());   // calling on null is an error, not a crash
        r.Run("t_badOption", &e);
        CHECK(Has(e, "type has no option 'Laser'"));
    }

    // ---- Destroyed objects and removed components read as null ----
    {
        Rig r("var h = null;\n"
              "function t_grab() { h = GameObject.Find(\"Other\").GetComponent(\"Light\"); return h.intensity; }\n"
              "function t_isNull() { return h == null; }\n"
              "function t_truthy() { if (h) { return 1; } return 0; }\n"
              "function t_useIt() { return h.intensity; }\n"
              "function t_callIt() { h.EffectiveColor(); }\n"
              "var c = null;\n"
              "function t_grabCam() { c = GetComponent(\"Camera\"); return c != null; }\n"
              "function t_camNull() { return c == null; }\n");
        CHECK(r.loaded);
        CHECK_NEAR(r.Run("t_grab").AsFloat(), 7.0f, 1e-5f);
        CHECK(!r.Run("t_isNull").AsBool());
        CHECK(r.Run("t_truthy").AsFloat() == 1.0f);
        r.scene.Destroy(r.other);
        r.scene.Update(1.0f / 60.0f);   // destruction lands at the end of the frame
        CHECK(r.Run("t_isNull").AsBool());
        CHECK(r.Run("t_truthy").AsFloat() == 0.0f);
        std::string e;
        r.Run("t_useIt", &e);
        CHECK(Has(e, "line 5: "));
        CHECK(Has(e, "the Light is null"));
        r.Run("t_callIt", &e);
        CHECK(Has(e, "the Light is null"));
        CHECK(r.Run("t_grabCam").AsBool());
        CHECK(r.lamp->RemoveComponent(r.cam));
        CHECK(r.Run("t_camNull").AsBool());
    }

    // ---- Review B1: no file, network, platform or clipboard method is reachable (gen_script_bindings.py DENY_*) ----
    {
        const char* deniedMethods[] = {"Terrain.ExportHeightmap", "Terrain.ImportHeightmap", "NavMeshSurface.Save",
                                       "NavMeshSurface.Load", "BuilderMode.SaveModel", "BuilderMode.SaveMap",
                                       "VehicleController.ToFile", "VehicleController.LoadPresetText",
                                       "ScriptComponent.SetPath", "ScriptComponent.LoadFile",
                                       "VisualScriptComponent.SetPath", "VisualScriptComponent.LoadFromFile",
                                       "UIInputField.PasteFromClipboard", "UIInputField.CopyToClipboard",
                                       "AudioSource.OpenStream", "Character.SaveClipsToFile", "Character.LoadClipsFromFile"};
        const char* deniedClasses[] = {"NetworkManager", "NetworkObject", "NetworkSync", "NetworkPlayerSpawner",
                                       "NetValidator", "SteamManager", "UIFilePicker", "UIHotReload"};
        int found = 0;
        for (const sb::ClassBinding& cls : sb::AllClasses()) {
            for (const char* dc : deniedClasses)
                if (cls.name == dc && !cls.methods.empty()) { std::printf("  %s has bound methods\n", dc); ++found; }
            for (const sb::MethodBinding& m : cls.methods)
                for (const char* dm : deniedMethods)
                    if (cls.name + "." + m.name == dm) { std::printf("  %s is bound\n", dm); ++found; }
        }
        CHECK(found == 0);
    }

    // ---- Review B2: enum arguments in their range, counts and indexes capped (no gigabyte allocations) ----
    {
        Rig r("function t_count() { var l = GetComponent(\"LineRenderer\"); l.SetPositionCount(2000000000); }\n"
              "function t_index() { var l = GetComponent(\"LineRenderer\"); l.SetPosition(2000000000, vec3(0, 0, 0)); }\n"
              "function t_fine() { var l = GetComponent(\"LineRenderer\"); l.SetPositionCount(3); return 1; }\n");
        CHECK(r.loaded);
        r.lamp->AddComponent<LineRenderer>();
        std::string e;
        r.Run("t_count", &e);
        CHECK(Has(e, "LineRenderer.SetPositionCount(): argument 1 must be -1 to 1048576"));
        r.Run("t_index", &e);
        CHECK(Has(e, "LineRenderer.SetPosition(): argument 1 must be -1 to 1048576"));
        CHECK(r.Run("t_fine").AsFloat() == 1.0f);
        Scene s("Enum");
        GameObject* g = s.CreateGameObject("E");
        if (const sb::ClassBinding* hs = sb::FindClass("HealthStat")) {
            Component* c = hs->add ? hs->add(*g) : nullptr;
            if (c) {
                vs::VsValue out;
                std::string err;
                CHECK(sb::CallOn(*c, *hs, "SetDeathAction", {vs::VsValue{99.0f}}, out, err) == sb::Result::Error);
                CHECK(Has(err, "argument 1 must be 0 to"));
                CHECK(sb::CallOn(*c, *hs, "SetDeathAction", {vs::VsValue{1.0f}}, out, err) == sb::Result::Ok);
            } else CHECK(false);
        } else CHECK(false);
    }

    // ---- Parity: every bound class is found by its name and every method is reachable and callable ----
    {
        const auto& classes = sb::AllClasses();
        CHECK(classes.size() > 250);
        int methods = 0, called = 0, addable = 0, badLookup = 0, ran = 0;
        const bool trace = std::getenv("OKAY_BIND_TRACE") != nullptr;   // name each call, to find one that crashes
        for (const sb::ClassBinding& cls : classes) {
            if (sb::FindClass(cls.name) != &cls) { ++badLookup; std::printf("  FindClass(%s) failed\n", cls.name.c_str()); }
            CHECK(sb::ClassByKey(sb::KeyOf(cls)) == &cls);
            for (const sb::MethodBinding& m : cls.methods) {
                ++methods;
                CHECK(m.fn != nullptr);
                CHECK(m.required >= 0 && m.required <= static_cast<int>(m.params.size()));
            }
            if (!cls.add) continue;
            // A fresh object of its own per class: add the component, then reach every method through the dispatch
            // with a wrong argument count, which must give a clear error and must not run the method.
            Scene s("Parity");
            GameObject* g = s.CreateGameObject("P");
            Component* c = cls.add(*g);
            if (!c) continue;
            ++addable;
            vs::VsValue h = sb::MakeHandle(c, &cls);
            CHECK(sb::Resolve(h) == c);
            for (const sb::MethodBinding& m : cls.methods) {
                std::vector<vs::VsValue> tooMany(m.params.size() + 1, vs::VsValue{1.0f});
                vs::VsValue out;
                std::string err;
                const sb::Result res = sb::CallOn(*c, cls, m.name, tooMany, out, err);
                if (res == sb::Result::Ok) continue;   // a longer overload of the same name took them
                if (res != sb::Result::Error || err.empty()) { std::printf("  %s.%s not reachable\n", cls.name.c_str(), m.name); CHECK(false); }
                ++called;
            }
            // Every read-only (const) method really runs, with a sample argument of each kind it takes.
            for (const sb::MethodBinding& m : cls.methods) {
                if (!m.isConst) continue;
                std::vector<vs::VsValue> args;
                for (sb::ArgKind k : m.params) args.push_back(Sample(k, g));
                vs::VsValue out;
                std::string err;
                if (trace) { std::fprintf(stderr, "call %s.%s\n", cls.name.c_str(), m.name); std::fflush(stderr); }
                const sb::Result res = sb::CallOn(*c, cls, m.name, args, out, err);
                if (res != sb::Result::Ok) { std::printf("  %s.%s: %s\n", cls.name.c_str(), m.name, err.c_str()); CHECK(false); }
                ++ran;
            }
        }
        CHECK(badLookup == 0);
        std::printf("  parity: %d classes (%d addable), %d methods, %d dispatch checks, %d read-only methods run\n",
                    (int)classes.size(), addable, methods, called, ran);
        CHECK(methods > 2000);
    }

    TEST_MAIN_RESULT();
}
