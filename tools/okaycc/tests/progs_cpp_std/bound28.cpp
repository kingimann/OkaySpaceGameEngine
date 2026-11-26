// Round 28: lower_bound and upper_bound with a comparison, and v.insert(it, {a, b}) of one aggregate
// element (not an initializer_list of them; Animation/AnimationCurve.hpp:33).
#include <algorithm>
#include <vector>
#include <cstdio>
struct Keyframe { float time; float value; };
int main() {
  std::vector<Keyframe> keys = {{0, 1}, {1, 2}, {3, 4}};
  float time = 2, value = 9;
  auto it = std::lower_bound(keys.begin(), keys.end(), time, [](const Keyframe& k, float t) { return k.time < t; });
  keys.insert(it, {time, value});
  auto up = std::upper_bound(keys.begin(), keys.end(), 1.0f, [](float t, const Keyframe& k) { return t < k.time; });
  for (auto& k : keys) printf("%g:%g ", k.time, k.value);
  printf("| %d\n", (int)(up - keys.begin()));
}
