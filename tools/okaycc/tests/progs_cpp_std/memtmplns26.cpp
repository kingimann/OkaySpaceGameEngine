// A member function template's instance reads names from the namespace the class is in ([temp.res]
// p10): sf::FacadeGuard inside okay::Save::Locked (Core/SaveData.hpp, -> decltype(f()) with F &&f).
#include <cstdio>
namespace okay {
namespace sf { struct FacadeGuard { FacadeGuard() { std::printf("in\n"); } ~FacadeGuard() { std::printf("out\n"); } }; }
struct Save {
    static int Get() { return Locked([&] { return 4; }); }
private:
    template <class F> static auto Locked(F &&f) -> decltype(f()) {
        sf::FacadeGuard lock;
        return f();
    }
};
}  // namespace okay
int main() { std::printf("%d\n", okay::Save::Get()); }
