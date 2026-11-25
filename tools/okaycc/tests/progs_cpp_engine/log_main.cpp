// Driver for engine/include/okay/Core/Log.hpp alone (round 23: static member function templates with
// packs, a fold over an ostringstream, nested enum class types as Log::Level, static bodies that use
// nested types declared later, thread_local, a std::function sink). The header is the engine part
// tested. Prints what the sink is given and what the levels and categories decide.
#include "okay/Core/Log.hpp"

#include <cstdio>
#include <string>

static int gCount = 0;

int main() {
    using okay::Log;
    Log::console = false;  // only the sink prints (stdout is compared with g++)
    Log::sink = [](Log::Level lv, const std::string& msg) {
        ++gCount;
        std::printf("sink %d [%s]\n", (int)lv, msg.c_str());
    };
    Log::Info("hello ", 42, " pi=", 3.5);
    Log::Trace("trace ", 'c');
    Log::Warning("warn");
    Log::Error("err ", -7, ' ', 10u);
    OKAY_INFO("macro ", std::string("str"));
    std::printf("configure=%d\n", Log::Configure("warn,net=trace,physics=error,bogus"));
    Log::Info("dropped");
    Log::Warning("kept");
    Log::Category net("net");
    Log::Category phys("physics");
    net.Trace("net trace ", 1);
    phys.Warning("phys warn dropped");
    phys.Error("phys error ", 2);
    std::printf("eff net=%d physics=%d other=%d\n", (int)Log::EffectiveLevel("net"),
                (int)Log::EffectiveLevel("physics"), (int)Log::EffectiveLevel("x"));
    Log::ClearCategoryLevels();
    net.Trace("net trace dropped");
    Log::SetCategoryLevel("net", Log::Level::Info);
    net.Info("net info ", net.Name());
    std::printf("count=%d file=%d failures=%d\n", gCount, (int)Log::FileOpen(), (int)Log::FileWriteFailures());
    return 0;
}
