// Round 28: casts and other integer constant expressions in a static table (Input/Input.hpp:376).
#include <cstdio>
enum Key : char { KeyUp = 1, KeyDown = 2 };
struct KeyEntry { const char* name; char code; };
static const KeyEntry* Find(int k) {
  static const KeyEntry kTable[] = {
    {"space", ' '}, {"up", KeyUp}, {"down", KeyDown}, {"esc", static_cast<char>(27)}, {"x", (char)('A' + 2)},
  };
  return &kTable[k];
}
int main() { for (int i = 0; i < 5; ++i) printf("%s %d\n", Find(i)->name, Find(i)->code); }
