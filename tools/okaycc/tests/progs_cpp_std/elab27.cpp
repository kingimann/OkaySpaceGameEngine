// A class first named in a parameter list (void f(class Camera *c)) and declared again with
// "class Camera;": the name is a type name after that ([basic.scope.pdecl] p7, [dcl.type.elab];
// Scene/Scene.hpp:356 after Components/Character.hpp, 10 engine files).
#include <cstdio>
namespace okay {
class Character {
public:
    int k = 3;
    void FinishTwoHands(class Camera *cam, float depth);
};
class Camera;
class Scene {
public:
    Camera *mainCamera = nullptr;
};
class Camera { public: int id = 7; };
void Character::FinishTwoHands(Camera *cam, float depth) { std::printf("%d %g\n", cam ? cam->id : -1, depth); }
}
int main() {
    okay::Scene s;
    okay::Camera c;
    s.mainCamera = &c;
    okay::Character ch;
    ch.FinishTwoHands(s.mainCamera, 0.5f);
}
