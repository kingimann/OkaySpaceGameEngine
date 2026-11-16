# okay::sync: our own threads, mutexes, conditions and semaphores

Replaces the SDL thread family (`SDL_CreateThread`, `SDL_mutex`, `SDL_cond`, `SDL_sem`) one launcher subsystem at a time.
Header `engine/include/okay/Sync/Sync.hpp`, code `engine/src/Sync/Sync.cpp` (built into `okay_platform`). Written from the Win32
and POSIX threads documentation; nothing copied from SDL or any other project.

## What is in it

| Piece | Windows | Linux |
|---|---|---|
| `Mutex` (recursive, like `SDL_mutex`) | `CRITICAL_SECTION` | `pthread_mutex_t`, recursive |
| `Cond` (`Wait`, `WaitFor(ms)`, `Signal`, `Broadcast`) | `CONDITION_VARIABLE` + `SleepConditionVariableCS` | `pthread_cond_t` on `CLOCK_MONOTONIC` (a timeout does not follow the wall clock) |
| `Semaphore` | a mutex, a condition and a count (same code on both) | same |
| `Thread` (`Start`, `Join` returns the function's int, `Detach`; the destructor detaches) | `CreateThread` | `pthread_create` (names up to 15 characters) |
| `LockGuard`, `SleepMs` | | |

No system header is in `Sync.hpp`. MinGW's "win32" thread model cannot compile `<thread>`, `<mutex>`, `std::condition_variable`
or `std::call_once` (CLAUDE.md rule 21a), and none of them is used. Not wrapped, on purpose: atomics (`std::atomic` needs no
thread library and replaces `SDL_atomic_t`), thread-local storage (the `thread_local` keyword works on both).

A `Thread` and its running function share one small context that whoever lets go last frees, so `Detach` and an early
`Join` are both safe. `Cond::Wait` on a mutex that the caller locked twice does not release it fully (as with SDL): lock once around a wait.

## Switch (OFF by default)

`-DOKAY_OWN_SYNC=ON` at configure time. With it the launcher's **Versions page** (`launcher/VersionsUI.cpp`: the index download
thread, the install thread and the progress mutex) uses `okay::sync`; without it, SDL exactly as before. The first time a job
thread starts, one log line says `sync: okay::sync threads in the Versions page (OKAY_OWN_SYNC)`. Nothing else in the launcher is moved.

## Tests

`ctest -R '^sync$'` (`tests/test_sync.cpp`, 77 checks, only `okay::sync` and `std::atomic`, no `<thread>`): 8 threads x 100,000
locked increments; recursion and `TryLock` across threads; 4 producers and 4 consumers moving 100,000 values through a
condition queue; 20,000 ping-pong round trips between two threads (a lost wake-up would hang it); broadcast to 8 waiters;
`WaitFor` timeout (at least 55 ms of 60) and an early wake-up; semaphore with 4 posters and 4 waiters (20,000 posts, each
consumed once), timeouts, `TryWait`; thread results, 1,000 start/join cycles, 250 detached threads, a null function.
Run as written: native, under AddressSanitizer + UBSan, under ThreadSanitizer (3 runs, no report), and built and linked with MinGW
(win32 thread model). `-DOKAY_SYNC_TSAN=ON` adds the ctest `sync_tsan` (needs libtsan; off by default).
**The Windows implementation was compiled and linked but never run; the CI Windows job does not run `test_sync.exe` yet.**

## What is left (SDL thread uses outside the moved page)

Counted with grep on this branch (words, not call sites):

| File | Uses | Notes |
|---|---|---|
| `launcher/main.cpp` | 67 `SDL_CreateThread`, 56 `SDL_DetachThread`, 12 `SDL_WaitThread`, 58 `SDL_CreateMutex`, 5 `SDL_DestroyMutex`, 170 `SDL_LockMutex`, 176 `SDL_UnlockMutex`, about 48 `SDL_Atomic*` / `SDL_atomic_t` | the bulk: downloads, avatar, friends, social links, publishing, git, updater state. Mostly the same worker shape (`int Worker(void*)`, a job struct, a mutex), so a mechanical move with `okay::sync::Thread::Start` + `Detach` and `std::atomic` is expected; the few `SDL_AtomicCAS` and `SDL_AtomicAdd` need a look |
| `launcher/VoiceChat.hpp` | 1 thread, 1 mutex (`Guard` class) and about 20 lock/unlock | next smallest; one thread, one mutex |
| `launcher/VersionsUI.cpp` | moved (switch) | the proof of the seam |
| editor, player, engine, shared | none | |

Not done: moving any of them; running the moved page with the switch on in a real launcher (only compile-checked here, with and
without the define); a condition or semaphore user in the launcher (there is none today, so those two are tested but unused).
