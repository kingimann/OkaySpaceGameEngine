// Round 23: typeid, std::type_info, std::type_index and dynamic_cast (ISO/IEC 14882 [expr.typeid],
// [type.info], [type.index], [expr.dynamic.cast]; layout after the Itanium C++ ABI 2.9.5). Names are
// the mangled type names g++ gives (for our own classes and the built-in types).
#include <cstdio>
#include <map>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>
struct Component {
    virtual ~Component() {}
    virtual int Id() const { return 0; }
};
struct Light : Component {
    int Id() const override { return 1; }
};
struct SpotLight : Light {
    int Id() const override { return 2; }
};
struct Mesh : Component {};
struct Plain {
    int x = 0;
};
namespace game {
struct Thing : Component {};
}  // namespace game
template <typename T>
static const char *nameOf() { return typeid(T).name(); }

int main() {
    std::printf("%s %s %s %s %s\n", typeid(int).name(), typeid(double).name(), typeid(char).name(),
                typeid(unsigned long).name(), typeid(bool).name());
    std::printf("%s %s %s\n", typeid(Light).name(), typeid(game::Thing).name(), typeid(const char *).name());
    Light l;
    SpotLight s;
    Mesh m;
    Plain p;
    Component *items[] = {&l, &s, &m};
    for (Component *c : items)
        std::printf("dyn %s light=%d spot=%d mesh=%d\n", typeid(*c).name(), (int)(typeid(*c) == typeid(Light)),
                    (int)(dynamic_cast<SpotLight *>(c) != nullptr), (int)(dynamic_cast<Mesh *>(c) != nullptr));
    Component &ref = s;
    std::printf("ref %s static %s plain %s\n", typeid(ref).name(), typeid(Component).name(), typeid(p).name());
    Light *asLight = dynamic_cast<Light *>(items[1]);
    std::printf("spot as light id=%d\n", asLight ? asLight->Id() : -1);
    Component *none = nullptr;
    std::printf("null cast %d\n", (int)(dynamic_cast<Light *>(none) == nullptr));
    Light *up = &s;
    Component *base = dynamic_cast<Component *>(up);
    std::printf("upcast id=%d\n", base->Id());
    std::printf("tmpl %s %s\n", nameOf<float>(), nameOf<Mesh>());
    std::printf("eq %d ne %d before %d\n", (int)(typeid(int) == typeid(int)), (int)(typeid(int) != typeid(long)),
                (int)(typeid(Light).before(typeid(Mesh)) == (std::string(typeid(Light).name()) < typeid(Mesh).name())));
    std::map<std::type_index, int> counts;
    std::unordered_map<std::type_index, std::string> names;
    for (Component *c : items) {
        counts[std::type_index(typeid(*c))]++;
        names[typeid(*c)] = typeid(*c).name();
    }
    counts[typeid(Light)] += 10;
    std::printf("counts light=%d spot=%d mesh=%d size=%d\n", counts[typeid(Light)], counts[typeid(SpotLight)],
                counts[typeid(Mesh)], (int)counts.size());
    std::printf("names %s %d\n", names[typeid(Mesh)].c_str(), (int)names.size());
    std::type_index ti = typeid(void);
    std::printf("void %s %d\n", ti.name(), (int)(ti == std::type_index(typeid(void))));
    std::printf("hash eq %d\n", (int)(typeid(Light).hash_code() == typeid(l).hash_code()));
    return 0;
}
