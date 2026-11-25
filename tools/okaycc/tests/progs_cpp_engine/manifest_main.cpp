// Driver for engine/src/Modules/ModuleManifest.cpp (round 22: istringstream, getline, assignment of
// a whole struct of strings and vectors). Prints what Parse reads from good and broken manifests.
#include "okay/Modules/ModuleManifest.hpp"

#include <cstdio>
#include <string>
#include <vector>

static void list(const char *what, const std::vector<std::string> &v) {
    std::printf("  %s:", what);
    for (const std::string &s : v) std::printf(" [%s]", s.c_str());
    std::printf("\n");
}

static void parse(const std::string &text) {
    okay::ModuleManifest m;
    std::string err;
    bool ok = okay::ModuleManifest::Parse(text, m, &err);
    std::printf("ok=%d err=[%s] name=[%s] title=[%s] area=[%s] v=%d default=%d core=%d group=[%s]\n", ok, err.c_str(),
                m.name.c_str(), m.title.c_str(), m.area.c_str(), m.version, m.defaultOn, m.core, m.vsGroup.c_str());
    list("requires", m.requires_);
    list("provides", m.provides);
    list("components", m.components);
    list("sources", m.sources);
}

int main() {
    parse("# health module\nname = health_damage\ntitle = Health and damage\narea=Gameplay\nversion = 3\n"
          "default = off\nrequires = core, input ,, physics\nprovides=Damage\n"
          "components = Health,DamageZone\nsources = src/Health/*.cpp\tinclude/Health/*.hpp  extra.cpp\n"
          "vs_group = Combat\nunknown_key = ignored\n");
    parse("name = core_bits\ncore = yes\ndefault = no\r\n");
    parse("title = no name here\n");
    parse("name = Bad-Name\n");
    parse("");
    std::printf("%d %d %d\n", okay::ModuleManifest::ValidName("abc_1"), okay::ModuleManifest::ValidName("ABC"),
                okay::ModuleManifest::ValidName(std::string(49, 'a')));
    std::vector<std::string> parts = okay::ModuleManifest::SplitList(" a | b|| c ", '|');
    list("split", parts);
    return 0;
}
