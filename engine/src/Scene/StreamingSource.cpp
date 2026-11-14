#include "okay/Scene/StreamingSource.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include <cmath>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>

namespace okay::worldpart {

static float Clean(float v) { return (std::isfinite(v) && v > 0.0f) ? v : 0.0f; }

float StreamingSource::EffectiveLoadRadius() const { return Clean(loadRadius); }
float StreamingSource::EffectiveUnloadRadius() const {
    const float l = EffectiveLoadRadius(), u = Clean(unloadRadius);
    return u > l ? u : l;
}
float StreamingSource::EffectivePriority() const { return (std::isfinite(priority) && priority > 0.0f) ? priority : 1.0f; }

void RegisterStreamingSerializers() {
    static std::once_flag once;
    std::call_once(once, [] {
        ComponentSerializerRegistry::Entry e;
        e.name = "streamingsource";
        e.write = [](const GameObject& go, std::ostream& out) {
            for (const StreamingSource* s : go.GetComponents<StreamingSource>()) {
                if (!s) continue;
                std::ostringstream line;
                line << std::setprecision(std::numeric_limits<float>::max_digits10)
                     << "  streamingsource load " << s->loadRadius << " unload " << s->unloadRadius
                     << " priority " << s->priority << " enabled " << (s->enabled ? 1 : 0) << "\n";
                out << line.str();
            }
        };
        e.read = [](GameObject& go, std::istream& in) {
            auto* s = go.AddComponent<StreamingSource>();
            std::string key;
            while (in >> key) {
                float v = 0.0f;
                if (!(in >> v)) break;
                if (key == "load") s->loadRadius = v;
                else if (key == "unload") s->unloadRadius = v;
                else if (key == "priority") s->priority = v;
                else if (key == "enabled") s->enabled = v != 0.0f;
            }
        };
        ComponentSerializerRegistry::Register(std::move(e));
    });
}

} // namespace okay::worldpart
