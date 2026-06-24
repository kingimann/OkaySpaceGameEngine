#include "test_framework.hpp"
#include <Okay.hpp>
using namespace okay;
int main(){
  RUN_SUITE("worldtoscreen");
  Scene s("S");
  GameObject* camgo = s.CreateGameObject("Cam");
  Camera* c = camgo->AddComponent<Camera>();
  // default transform at origin; engine cameras look down -Z
  Vec2 sp; float depth=0;
  bool ok = c->WorldToScreen({0,0,-10}, 800, 600, sp, &depth);
  CHECK(ok);
  CHECK(depth>0);
  CHECK_NEAR(sp.x, 400.0f, 2.0f);
  CHECK_NEAR(sp.y, 300.0f, 2.0f);
  // a point above center should be higher on screen (smaller y)
  Vec2 sp2; bool ok2 = c->WorldToScreen({0,3,-10}, 800,600, sp2);
  CHECK(ok2); CHECK(sp2.y < 300.0f);
  // behind the camera -> false
  Vec2 sp3; CHECK(!c->WorldToScreen({0,0,10}, 800,600, sp3));
  TEST_MAIN_RESULT();
}
