// Round 28: return f(); in a template whose return type is decltype(f()) and turns out void, with a
// local to destroy (Core/SaveData.hpp:414), and a set built from a range (Render/Mesh.hpp:2114).
#include <cstdio>
#include <set>
#include <vector>
struct G { G() { printf("lock\n"); } ~G() { printf("unlock\n"); } };
struct S {
  template <class F> static auto Locked(F&& f) -> decltype(f()) { G lock; return f(); }
};
void hello() { printf("hello\n"); }
void viaVoid() { G g; return hello(); }
int main() {
  S::Locked([] { printf("in\n"); });
  int x = S::Locked([] { return 5; });
  viaVoid();
  std::vector<int> v = {3, 1, 3, 2};
  std::set<int> s(v.begin(), v.end());
  printf("%d %d\n", x, (int)s.size());
}
