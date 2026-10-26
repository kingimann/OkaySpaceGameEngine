# OkaySpace roadmap

What is being worked on right now and what comes next. Kept current by the main
(coordinating) Claude session; each item links its PR once one exists.

_Last updated: 2026-10-26 00:20 UTC_

## Releases

| Version | State | What's in it |
|---|---|---|
| 2.818.0 | live in the launcher | Walk fix for the one-click humanoid (#283), visual-script and script-language fixes, robustness fixes #272–#279, security #254 |
| 2.819.0 | building now (release workflow on main) | Shared in-game UI renderer (#282), long-expression crash fix (#287), physics dispatch crash fix (#292), network hardening (#288), Linux server build fix |
| 2.820.0 | next | The default player's walk/run rewrite (PR1), launcher project deletion |

## Now: main session

### PR1: the default player's walk and run
Part 1 §1–2. Local, building and testing.

- [x] Root cause: the stance knee was always folded (37–63° at every speed) by a constant hip drop, and "walk" at 4.5 m/s was really a jog.
- [x] Walk and run have their own styles:
  - **Walk:** one foot always down, heel strike, nearly straight stance leg (about 14° knee bend). Above ~0.85 m steps the cadence quickens instead.
  - **Run:** flight phase, forefoot strike, legs that scissor behind and forward, and a harder arm pump.
- [x] One continuous gait phase for every gait state. Walk↔run never moves the feet; the two styles blend over 0.2 s on live poses.
- [x] Crossfades between built-in states (idle↔walk, crouch, jump, land) run on live poses, not a frozen snapshot.
- [x] Speed is measured with last frame's dt and lightly smoothed, so the stride doesn't shimmer at variable frame rates.
- [x] The height slider scales the body uniformly. The stride matches at any height and the parts don't shear.
- [x] CharacterController3D, ThirdPersonShooter, TopDown, ClickToMove and NPC controllers all go through `Character::DriveLocomotion`. Every template now gets speed-matched strides, strafe/backpedal and jump/fall poses.
- [ ] Full test suite, before/after renders, PR.

### Launcher: delete projects
- [x] On a project card, the "…" button or a right-click opens two options:
  - "Remove from list".
  - "Delete project…": the folder goes to the Recycle Bin on Windows. Safety checks refuse anything that isn't a project folder, and refuse while the editor is running.
- [ ] PR after PR1.

## Next: main session
1. **PR2, the `Animator` component for the player** (part 1 §3).
   - A locomotion state machine with these states: Idle, Walk, Run, Jump, Fall, Land, Crouch, Crouch Walk, Prone, Crawl, Hang.
   - Each state has a clip slot, playback speed, sync to movement with a reference speed, loop, blend time and IK weight.
   - "Walk starts at" and "Run starts at" thresholds, plus the gait knobs (stride, cadence, arm swing, lean, bounce, foot lift).
   - Added automatically to every player and to old scenes.
   - An inspector panel with an Edit-mode preview: pick a state, drag a speed slider.
2. **PR3:** the Animator graph window shows and edits the player's states, and highlights the live state in Play.
3. **PR4:** "Import from file…" brings a humanoid clip (glTF/GLB/FBX, Mixamo) into a state slot. It is converted rest-relative and stride-synced.
4. Release 2.820.0 as these land.

## Now: other sessions

Each session finishes its current work, then takes its items from the part 2 player-animation spec.

| Session | Current | Then (part 2 items) |
|---|---|---|
| UI | Rebase #285 onto main | 1 "Apply" makes a clip play forever · 2 edits revert on Play / clips-file path · 5 Animation tab (open clips, undo, start from built-in) · 6 AnimClip slerp + loop seam |
| Humanoid | Humanoid P0 (rig generator covering 9 rigs) | 3 imported clips turn smooth after reload · 4 save every FootIK/retarget field · 7 walk plays run + feet skate · 8 false landing at jump apex · 9 retarget delta-from-rest + clip-name matching · 10 FootIK sinks feet on the retarget path · 11 RootMotion component |
| Scripting | #281 statics fixes → #284 | 12 scripts find the model's animator + Play override · 13 AnimStateMachine vs driveByMovement · 14 clip events at t=0 |
| QA | #291 anti-cheat review fixes; #280 → #290 | 15 remote players skate / wrong walk-run |
| Release | #269/#270 anti-cheat fixes; sanitizer CI widening | 16 2D sprites: named clips (idle/run/jump) |
| Robustness | Scene-record follow-ups; #276 loader gaps | 17 per-frame cache rebuilds and allocations · 18 IK polish |

## Open PRs

| PR | What | State |
|---|---|---|
| #280 → #290 | Visual script save / paste / compile fixes | #280 CI running |
| #285 | Editor UI preview through the engine renderer | Needs a rebase onto main |
| #281 → #284 | Typed OkayScript statics, `$"..."` strings | Two review fixes pending |
| #291 | Anti-cheat P1 server checks | Needs work (review posted on the PR) |
| #269 → #270 | Anti-cheat P3/P4 | Must-fix list in progress |

## Later
- Script language: classes; `GetComponent<MyScript>()` with a per-scene script library.
- Anti-cheat P1/P2 components (AntiCheatConfig, NetValidator, OnCheatDetected / OnValidateRpc nodes).
- The humanoid "import any human, press Play" plan, P1–P3.
- In-game UI plan, P1–P4.
