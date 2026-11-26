// Round 28: pointers to data members ([dcl.mptr], [expr.unary.op] p3, [expr.mptr.oper]):
// T C::* declarations (members, parameters, lambda parameters), &C::m, obj.*pm, p->*pm, a null one,
// comparisons, as the engine's tuning tables use them (Components/VehicleTuning.hpp:118,
// Components/UINavigation.hpp:292).
#include <cstdio>
#include <string>
#include <vector>

struct Base {
    int id = 1;
};
struct Tuning : Base {
    float mass = 1200.0f;
    int gears = 5;
    bool abs = true;
    char key = 'w';
    std::string name = "car";

    struct Desc {
        const char* label;
        float Tuning::*f;
        int Tuning::*i;
        bool Tuning::*b;
        char Tuning::*k;
    };
    static const std::vector<Desc>& Table() {
        static const std::vector<Desc> t = [] {
            std::vector<Desc> v;
            auto F = [&](const char* n, float Tuning::*m) { v.push_back({n, m, nullptr, nullptr, nullptr}); };
            auto I = [&](const char* n, int Tuning::*m) { v.push_back({n, nullptr, m, nullptr, nullptr}); };
            auto B = [&](const char* n, bool Tuning::*m) { v.push_back({n, nullptr, nullptr, m, nullptr}); };
            auto K = [&](const char* n, char Tuning::*m) { v.push_back({n, nullptr, nullptr, nullptr, m}); };
            F("mass", &Tuning::mass);
            I("gears", &Tuning::gears);
            B("abs", &Tuning::abs);
            K("key", &Tuning::key);
            return v;
        }();
        return t;
    }
    double Get(const Desc* d) const {
        if (d->f) return this->*(d->f);
        if (d->i) return this->*(d->i);
        if (d->b) return (this->*(d->b)) ? 1.0 : 0.0;
        return (unsigned char)(this->*(d->k));
    }
    void Set(const Desc* d, double v) {
        if (d->f) this->*(d->f) = (float)v;
        else if (d->i) this->*(d->i) = (int)v;
        else if (d->b) this->*(d->b) = v != 0.0;
        else this->*(d->k) = (char)(int)v;
    }
};

static int length(const Tuning& t, const std::string Tuning::*s) { return (int)(t.*s).size(); }

int main() {
    Tuning t;
    for (const Tuning::Desc& d : Tuning::Table()) std::printf("%s=%g ", d.label, t.Get(&d));
    std::printf("\n");
    const auto& tab = Tuning::Table();
    t.Set(&tab[0], 900.5);
    t.Set(&tab[1], 6);
    t.Set(&tab[2], 0);
    t.Set(&tab[3], 'x');
    for (const Tuning::Desc& d : tab) std::printf("%s=%g ", d.label, t.Get(&d));
    std::printf("\n");

    int Tuning::*pg = &Tuning::gears;
    int Tuning::*pid = &Tuning::id;
    Tuning* p = &t;
    p->*pg += 2;
    std::printf("%d %d %d\n", t.gears, p->*pid, t.*pid);
    float Tuning::*none = nullptr;
    std::printf("%d %d %d\n", none == nullptr ? 1 : 0, pg != nullptr ? 1 : 0, pg == &Tuning::gears ? 1 : 0);
    auto len = [&](const std::string Tuning::*s) -> int { return (int)(p->*s).size(); };
    std::printf("%d %d\n", len(&Tuning::name), length(t, &Tuning::name));
    return 0;
}
