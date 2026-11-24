// Drives the engine's own engine/src/Core/BuildInfo.cpp, compiled unchanged by okay-cc (std::string,
// operator+, a converting constructor for a const std::string & argument). Compiler() differs on
// purpose (g++ says "GCC x.y", okay-cc defines no __GNUC__), so only lengths around it are printed.
#include "okay/Core/BuildInfo.hpp"

#include <cstring>
#include <string>

extern "C" int printf(const char *fmt, ...);

int main() {
    printf("version %s commit %s config %s platform %s arch %s\n", okay::BuildInfo::Version(),
           okay::BuildInfo::Commit(), okay::BuildInfo::Config(), okay::BuildInfo::Platform(),
           okay::BuildInfo::Architecture());
    std::string sum = okay::BuildInfo::Summary();
    printf("summary %d\n", (int)(sum.size() - std::strlen(okay::BuildInfo::Compiler())));
    const char *texts[7] = {"1.2.3", "10.0", "7", "1..2", "1.2.3.4", "", "1.x"};
    for (int i = 0; i < 7; ++i) {
        int a = -1, b = -1, c = -1;
        bool ok = okay::BuildInfo::ParseVersion(texts[i], a, b, c);
        printf("parse '%s' %d %d %d %d\n", texts[i], ok, a, b, c);
    }
    printf("compare %d %d %d %d %d\n", okay::BuildInfo::CompareVersions("1.2.3", "1.2.4"),
           okay::BuildInfo::CompareVersions("2.0", "1.9.9"), okay::BuildInfo::CompareVersions("dev", "0.0.1"),
           okay::BuildInfo::CompareVersions("dev", "dev"), okay::BuildInfo::CompareVersions("3.1", "3.1.0"));
    printf("compatible %d %d\n", okay::BuildInfo::IsCompatible("0.1.0"), okay::BuildInfo::IsCompatible("dev"));
    return 0;
}
