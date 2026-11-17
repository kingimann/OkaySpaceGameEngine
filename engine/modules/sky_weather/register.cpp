// Module sky_weather: its scene records (moved here unchanged from SceneSerializer.cpp, which now finds them through the
// ComponentSerializerRegistry). Linked only when the module is on.
#include "okay/Components/DayNightCycle.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/RecordIO.hpp"
#include <algorithm>

namespace okay {
namespace modules {
using rec::Quote; using rec::ReadQuoted; using rec::KvOut; using rec::KvIn;

void Register_sky_weather(ModuleContext&) {
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
