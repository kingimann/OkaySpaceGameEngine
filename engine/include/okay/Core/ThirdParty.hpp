#pragma once
// The one master switch back to the third-party libraries: OKAY_THIRD_PARTY=1 in the environment.
//
// Our own replacements (model readers first: owner approval 2026-11-17, "start switching them so i can test them out") are the
// default. A person who hits a problem sets OKAY_THIRD_PARTY=1 and every part that has a third-party fallback compiled in uses
// it again (Assimp for models; FBX has no fallback since ufbx was deleted on 2026-11-23). Each part still has its own per-part switch as well (OKAY_OWN_<FMT>=0).
//
// Semantics, kept exactly so every branch that uses this helper agrees:
//   * true only when the variable is set and its value is exactly "1";
//   * read on every call (not cached here), so a test can set and clear it; a caller that decides once may cache;
//   * code that has no third-party fallback compiled in ignores it (nothing else could do the work).
// Header-only, standard library only.
#include <cstdlib>

namespace okay {

inline bool ThirdPartyForced() {
    const char* e = std::getenv("OKAY_THIRD_PARTY");
    return e && e[0] == '1' && e[1] == '\0';
}

} // namespace okay
