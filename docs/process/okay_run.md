# okay_run: run a scene and look at what happened

> **What this is:** the one command agents and CI use to RUN the engine and check behaviour, instead of guessing from code.
>
> **Status:** added 2026-11-21 (branch `claude/verify-harness-1`). Linux, headless. Back to the [process index](README.md).

```
cmake --build build --target okay_run          # once (ninja -j2)
python3 scripts/okay_run.py SCENE --input IN.json --check CHECK.json [--out DIR]
```

What it runs: `tests/okay_run.cpp` opens the scene with the editor's loader, presses Play (`EditorState::Play`) and calls
`Tick(dt)` N times at a fixed step: the same `Scene::Update` the player and Play mode run, with input pushed through
`Input::FeedKeys` / `FeedMouse` / `FeedGamepad` like the windowed hosts do. No window, no GPU. Pictures come from the
software renderer (`okay::testing::RenderSceneToImage`).

Flags: `--frames N` (120), `--dt S` (1/60), `--trace NAME,NAME` (objects to record), `--shots F,F,end`, `--size WxH`
(640x360), `--bin PATH` (or `OKAY_RUN_BIN`; default `build/bin/okay_run`). Flags win over the input file.

## Input file (JSON)

```json
{"frames": 300, "trace": ["Player"], "shots": [0, 150, "end"],
 "events": [{"time": 0.5, "hold": "w", "seconds": 2.0}, {"time": 2.6, "tap": "space", "frames": 3},
            {"frame": 10, "press": "shift"}, {"frame": 40, "release": "shift"}, {"frame": 5, "look": [40, 0]},
            {"frame": 5, "mouse": [640, 360]}, {"frame": 6, "button": 0, "down": true}, {"frame": 7, "stick": [0, 1], "right": [0.5, 0]},
            {"frame": 20, "click": "Play Button"}, {"frame": 90, "shot": true}]}
```

`frame` or `time` (seconds) on every event. Key names are `Input::KeyFromName` names (w, space, shift, up, f5 ...).
`click` finds the screen-space UI button by object name and presses it for one frame. Example: `tests/scripts/okay_run/`.

## Check file (JSON)

`{"checks": [...]}`, or a `checks` list inside the input file. `object` is a traced name or `camera`; `field` is a path
such as `pos.y`, `vel`, `grounded`, `anim.0.state`, `anim.0.params.speed`, `fov`. `from` / `to` are frames (`end` allowed).

| expect | passes when |
|---|---|
| `ran` | every frame ran and Play did not stop early |
| `no_errors` | no error lines in the log (`ignore`: substrings; `warnings: true` counts warnings too) |
| `value` | `field` `op` `value` at frame `at` (default `end`) |
| `ever` / `always` | the comparison holds on some / every frame in the window |
| `moved` | distance between first and last frame of the window is within `min`..`max` |
| `max_step` | the largest frame-to-frame change of `field` is at most `max` (camera pops, teleports) |
| `camera_never_inside` | the main camera is never inside a solid collider other than its own body (box bounds test) |
| `frame_ms` | mean simulated frame time is at most `max` ms (this machine, CPU only) |

## Output folder

Default `$OKAY_SHOT_DIR/okay_run/<scene>`, else a new temp folder; never inside the repository (rule 4b).
`trace.jsonl` (one line per frame: `objects` with `pos`, `rot` (Euler degrees), `vel`, `grounded`, `active`, `button`
(hover, pressed, clicked), `anim` [state and params]; `camera` with `pos`, `rot`, `fov`, `inside`; `ms` the frame's wall time), `log.txt` (`[frame] level: text`),
`frame_NNNNN.png`, `summary.json` (frames run, errors, warnings, frame ms, exit), `events.txt`, `checks.json`.
Exit: 0 pass, 1 a check failed, 2 bad arguments, 3 the engine run failed.

## Limits

Pictures draw 3D meshes only (no sky, sprites, particles or UI; UI clicks still work). Software renderer, so not what the
GPU shows. Linux only; Windows unverified. Audio and networking not exercised. For the real windowed launcher or editor
use `scripts/run_app_script.sh` (Xvfb, `OKAY_TEST_SCRIPT`, needs `OKAY_ENABLE_TEST_HOOKS=ON`). Self-test:
`python3 tests/scripts/test_okay_run.py` (ctest `okay_run_selftest`).
