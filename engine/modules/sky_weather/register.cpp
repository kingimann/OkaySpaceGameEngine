// Sky and weather module startup: the DayNightCycle inspector table.
// It used to be registered with every other gameplay component. It now lives with the
// module, so a build that leaves sky_weather off does not offer those fields.
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/PropertyBuilder.hpp"
#include "okay/Components/DayNightCycle.hpp"

namespace okay {
namespace modules {

void Register_sky_weather(ModuleContext&) {
    constexpr double kBig = 100000.0;
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

} // namespace modules
} // namespace okay
