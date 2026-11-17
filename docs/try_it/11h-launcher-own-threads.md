---
id: 11h
group: appendix
row: | 11h | Launcher background work on our own threads | `OkaySpace.exe` (section "Launcher: threads on our own code") | Tested on Linux only |
---
## Launcher: threads on our own code (branch `claude2/launcher-own-threads`, not merged or released yet)

What changed: the launcher's background work (news, downloads, the update check, chat, voice, the Git buttons, every
"working..." job) used SDL's threads, locks and counters. It now uses our own (`launcher/LauncherThreads.hpp` over
`engine/src/Core/CoreThreads.hpp`). Nothing should look different. This is a check that it still behaves the same.

1. Start `OkaySpace.exe`. The start-up update check runs and the launcher opens. Expect the same as before (no hang on
   "Checking for updates").
2. **Home / What's New**: the news and link previews load. **Account**: sign in; your avatar loads.
3. **Versions**: install an editor version. The progress bar moves; **Cancel** stops it; installing again finishes.
4. Click the version number at the bottom of the left bar (tooltip "Check for updates"). The Software Update window
   answers ("up to date" or offers the update) and the launcher does not freeze.
5. **Create** page: right-click a project that is a Git folder and pick **Git: Pull**, or use **Commit & Push**, or
   **Clone** a repository. A message says it worked or failed, as before.
6. Chat: send a message and an image; start a voice call and the mic test; hang up.
7. Quit the launcher while a download runs. It closes within a few seconds and does not crash.

If any step hangs or crashes where the last release did not, that is this change: report the step.

**Status.** Tested on Linux only: `test_core_threads` (mutex under contention, recursive lock, a woken waiter, exact atomic
adds, join, detach, names; also clean under ThreadSanitizer), the launcher built natively, and `launcher/main.cpp` passed a
MinGW (Windows) syntax check. Not run on Windows: the steps above are unverified until you run them.
