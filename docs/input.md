# Input (internal record)

Not a guide. Where things are and what is missing; the details are in the headers and `docs/feature_map/08-input.md`.

## What exists
- Raw input and actions: `engine/include/okay/Input/Input.hpp`, `InputActions.hpp` (contexts, chords, response, timing, per player), `RemapModel.hpp`, `InputRecorder`, `InputDebug`, `Haptics`.
- Controllers: `GamepadInfo` (family from USB ids / name / host type), `ControllerMapping` (generic layout, Controller Setup, saved mapping), `PadProfiles`, `PadSlots`, `PlatformGamepad.hpp` (`PadSet`), optional `platform::Gamepad` extras (SDL backend: ids, raw axes, battery, lightbar, trigger rumble, touchpad, gyro). `OKAY_GENERIC_PADS=0` stops opening unmapped joysticks.
- Typed actions: `ActionSet` (value types, composites, processors, schemes, interactions with phases, maps with priority / consume, rebinding overrides), `InputAsset` (.okayinput text, default asset, migration of `InputActions`), `PlayerInput` (local players), `InputRuntime` + `ia_*` script calls, `InputBuffer` (buffer, coyote time), `ActionEditor` (logic of the editor window).
- Editor: `editor/InputActionsWindow.hpp` (View > Editors & Tools > Input Actions), `editor/InputDebugger.hpp` (Input panel). New Project writes `Assets/input.okayinput`.
- Playground station H (Controller) in `examples/input/input_playground.okayscene`; `examples/input/default.okayinput` equals the default asset (a test checks it).
- Tests: `controller_mapping`, `action_set`, `action_phases`, `input_asset`, `input_script_api`, `action_editor`, `input_playground`, plus the older input suites.

## What is missing
See the Input row in `docs/UNFINISHED.md`: Inspector Player Input component, visual-script nodes for `ia_*`, in-game rebinding screen on `ActionSet`, loading the project asset on Play, touch leaf inputs, per-binding interactions, adaptive triggers / HD haptics. Everything was only run with fake devices; the ImGui drawing was never run in a real editor window; nothing on Windows.

## Pictures
Not stored in the repository. `tools/input_shots.sh <imgui folder> [output folder]` draws them headless with ImGui (output defaults to `$OKAY_SHOT_DIR`).
