// Module sky_weather: its scene records (moved here unchanged from SceneSerializer.cpp, which now finds them through the
// ComponentSerializerRegistry). Linked only when the module is on.
#include "okay/Components/DayNightCycle.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/PropertyBuilder.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scripting/ScriptApiRegistry.hpp"
#include "okay/Scene/SceneRules.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/RecordIO.hpp"
#include <algorithm>

namespace okay {
namespace modules {
using rec::Quote; using rec::ReadQuoted; using rec::KvOut; using rec::KvIn;

namespace {
constexpr double kBig = 100000.0;   // the property tables' "no limit" (src/Scene/BuiltinPropertiesShared.hpp)
void RegisterDayNightProperties() {
    PropertyBuilder<DayNightCycle> b("DayNightCycle", "daynight", "Environment");
    b.AddFloat("dayLengthSeconds", &DayNightCycle::dayLengthSeconds).Range(0, kBig, 1).Tooltip("Real seconds for a full 24h day")
     .AddFloat("time", &DayNightCycle::time).Range(0, 24, 0.1).Tooltip("Current time of day, hours [0,24)")
     .AddBool("paused", &DayNightCycle::paused)
     .AddBool("controlSun", &DayNightCycle::controlSun).Tooltip("Drive the sun Light (intensity/color/ambient)")
     .AddBool("rotateSun", &DayNightCycle::rotateSun).Tooltip("Rotate the sun Light's transform across the sky")
     .AddBool("controlSky", &DayNightCycle::controlSky).Tooltip("Drive the main camera's background color")
     .AddFloat("dayIntensity", &DayNightCycle::dayIntensity).Range(0, 1000, 0.05)
     .AddFloat("nightIntensity", &DayNightCycle::nightIntensity).Range(0, 1000, 0.05)
     .AddFloat("dayAmbient", &DayNightCycle::dayAmbient).Range(0, 1, 0.01)
     .AddFloat("nightAmbient", &DayNightCycle::nightAmbient).Range(0, 1, 0.01)
     .AddColor("dayLight", &DayNightCycle::dayLight).Range(0, 1, 0.01).Tooltip("Midday white")
     .AddColor("nightLight", &DayNightCycle::nightLight).Range(0, 1, 0.01).Tooltip("Cool moonlight")
     .AddColor("skyDay", &DayNightCycle::skyDay).Range(0, 1, 0.01).Tooltip("Clear blue")
     .AddColor("skyHorizon", &DayNightCycle::skyHorizon).Range(0, 1, 0.01).Tooltip("Sunrise/sunset orange")
     .AddColor("skyNight", &DayNightCycle::skyNight).Range(0, 1, 0.01).Tooltip("Deep night");
    b.Commit();
}
} // namespace

void Register_sky_weather(ModuleContext&) {
    RegisterDayNightProperties();
    rules::AddSingletonRule("DayNightCycle", [](const Scene& s) {
        const auto all = s.FindObjectsOfType<DayNightCycle>();
        return std::vector<Component*>(all.begin(), all.end());
    });
    // time_of_day([hours]) on the scene's DayNightCycle: -1 when there is none.
    AddScriptApi("time_of_day", [](GameObject* self, std::vector<vs::VsValue>& a) -> vs::VsValue {
        Scene* s = self ? self->scene() : nullptr;
        DayNightCycle* dn = s ? s->FindObjectOfType<DayNightCycle>() : nullptr;
        if (!dn) return vs::VsValue{-1.0f};
        if (!a.empty()) dn->SetTime(a[0].AsFloat());
        return vs::VsValue{dn->Hour()};
    });
    {
        ComponentSerializerRegistry::Entry e;
        e.name = "daynight";
        e.write = [](const GameObject& go, std::ostream& out) {
    for (auto* c : go.GetComponents<DayNightCycle>()) {
        auto col = [&](const Color& k) { out << " " << k.r << " " << k.g << " " << k.b; };
        out << "  daynight " << c->dayLengthSeconds << " " << c->time << " " << (c->paused ? 1 : 0)
            << " " << (c->controlSun ? 1 : 0) << " " << (c->rotateSun ? 1 : 0) << " " << (c->controlSky ? 1 : 0)
            << " " << c->dayIntensity << " " << c->nightIntensity << " " << c->dayAmbient << " " << c->nightAmbient;
        col(c->dayLight); col(c->nightLight); col(c->skyDay); col(c->skyHorizon); col(c->skyNight);
        out << "\n";
    }
        };
        e.read = [](GameObject& go, std::istream& in) {
                    auto* c = go.AddComponent<DayNightCycle>();
                    int ps = 0, cs = 1, rs = 1, ck = 1;
                    in >> c->dayLengthSeconds >> c->time >> ps >> cs >> rs >> ck
                       >> c->dayIntensity >> c->nightIntensity >> c->dayAmbient >> c->nightAmbient;
                    c->paused = (ps != 0); c->controlSun = (cs != 0); c->rotateSun = (rs != 0); c->controlSky = (ck != 0);
                    auto col = [&](Color& k) { in >> k.r >> k.g >> k.b; k.a = 1.0f; };
                    col(c->dayLight); col(c->nightLight); col(c->skyDay); col(c->skyHorizon); col(c->skyNight);
        };
        ComponentSerializerRegistry::Register(std::move(e));
    }
}
} // namespace modules
} // namespace okay
