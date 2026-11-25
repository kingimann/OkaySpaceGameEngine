---
id: team-tooling
---
## Team tooling (not part of the Windows release)

How to try the collaboration tools ([collaboration.md](../process/collaboration.md)); these run on a Linux checkout and on GitHub, not in
`OkaySpace.exe`:

1. Open `WORK_BOARD.md` on the `work-board` branch (after the workflow has run once on main): it now has a **Claims** table and
   a **Collision report**.
2. `python3 scripts/find_existing.py tween` prints what already exists for "tween" (feature map, code, tests, branches).
3. Push a branch named `claude2/x` that adds `#include <mutex>` to a header under `engine/include`: the **Branch lint** check
   fails with the file and line and says to use `debugutil::SpinLock`.
4. Open a PR without a "How to try it" section: the **PR hygiene** check fails and says what to add.
