# Developer + QA sessions working together

Two Claude Code sessions work on this repository at the same time:

- the **developer** session builds features and fixes on its own branch and
  opens pull requests;
- the **QA** session tests every pull request the developer hands it, tries to
  break it, and reports what it finds.

They talk through two channels:

| Direction | Channel |
| --- | --- |
| developer → QA | a one-shot scheduled message delivered into the QA session ("test PR #N at sha X: what changed, what to try") — cloud sessions can't message each other directly, so the developer schedules a message into the QA session a minute or two ahead |
| QA → developer | comments on the pull request under test (the developer is subscribed to the PR, so every comment wakes it up) |

## The loop

1. The developer pushes a branch, opens a draft PR and messages QA with the PR
   number, what changed, and what is risky.
2. QA checks out the PR branch, builds it, runs the tests, then tests the
   feature for real (scripted probes, the editor under Xvfb, save/reload,
   edge cases, old scenes still loading).
3. QA posts **one PR comment per bug** (format below), then one **verdict**
   comment.
4. The developer fixes, pushes, replies on the comment, and messages QA to
   re-check. QA re-tests only what changed plus a quick regression pass, and
   posts a new verdict.
5. The developer marks the PR ready and merges only after a `PASS` verdict on
   the latest commit and green CI.

## Bug comment format

```
[QA] BUG <short title>
Severity: blocker | major | minor | polish
Commit: <sha tested>
Repro:
1. ...
Expected: ...
Actual: ...
Evidence: <test output, numbers, log lines>
Suspected cause: <file:line, if known>
```

## Verdict comment format

```
[QA] VERDICT: PASS | FAIL
Commit: <sha>
Tested: <bullet list of what was exercised>
Not tested: <anything skipped and why>
Open bugs: <links to the bug comments, or none>
```

## Rules for the QA session

- Never push to the developer's branch, never merge or close PRs, never
  release, never touch Supabase, keys or tokens.
- Regression tests QA writes go on its **own** branch in a separate draft PR
  titled `QA: regression tests for #N`, so the developer can pull them in.
- Report what was actually observed. If something could not be tested (no
  display, missing asset), say so in the verdict instead of guessing.
- Find the root cause where possible (file and line), but leave the fix to
  the developer unless asked.

## Building and testing (Linux container)

```bash
sudo apt-get install -y cmake g++ ninja-build libsdl2-dev libsodium-dev xvfb xdotool imagemagick
cmake -S . -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build-tests && ctest --test-dir build-tests -j4 --output-on-failure
cmake -S . -B build-editor -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DOKAY_BUILD_EDITOR=ON -DOKAY_BUILD_TESTS=OFF -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_LAUNCHER=ON
ninja -C build-editor OkayEngine okay-player
./build-editor/bin/OkayEngine --selftest
```

Driving the editor: start `Xvfb :98 -screen 0 1600x1000x24 &`, run the editor
with `DISPLAY=:98`, use `xdotool` (mousedown/mouseup for clicks) and
`import -window root shot.png` for screenshots. Floating ImGui windows can
steal keyboard focus; re-focus with
`xdotool windowfocus $(xdotool search --name "OkaySpace Editor" | head -1)`.

Quick engine probes: a small `.cpp` linked against
`build-tests/engine/libokay_engine.a` (`-Iengine/include -lsodium
-DOKAY_HAVE_SODIUM -lpthread`) can build a scene from `Templates::...`, feed
input with `Input::FeedKeys` / `FeedGamepad` / `FeedMouse`, step
`scene.Update(1/60.f)` and print numbers.

Shell hygiene: never `pkill -f` (it can kill the session); use
`pgrep OkayEngine | xargs -r kill -9`. Don't use foreground `sleep`.
