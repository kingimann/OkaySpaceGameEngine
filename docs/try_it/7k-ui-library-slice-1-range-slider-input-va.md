---
id: 7k
row: | 7k | UI library slice 1: range slider, input validation, aspect-ratio box | File > Examples > UI library > ui_library_1 (then Play), or GameObject > UI > Controls and Layout & Overlays; file `examples\ui_library\ui_library_1.okayscene` | New in this change; headless pictures only, real window unverified |
---
## 7k. UI library slice 1: range slider, input validation, aspect-ratio box

**What it is.** A UI screen with three cards that show the parts added in slice 1 of the in-game UI component library
([ui_library.md](ui/ui_library.md); [ui_component_audit.md](ui/ui_component_audit.md) says what already existed): a range slider
with two handles, input boxes with rules (email, number range, password and repeat) and a 16:9 frame that keeps its shape.

**Steps.**
1. Open the editor. **File > Examples > UI library (every in-game UI part) > ui_library_1**. (Or **File > Open...** with
   `<install folder>\examples\ui_library\ui_library_1.okayscene`; the installer puts it in `%LOCALAPPDATA%\OkaySpace\examples\ui_library\`.)
2. Press **Play**. The Game view shows "Range slider", "Input validation" and "Aspect ratio box (16:9)" cards with hints.
3. Drag the handles of "Price range". The numbers above the track read `$120 to $380` and change in steps of 10. The handles
   never cross. Drag the "Level range" handles: whole numbers, at least 3 apart.
4. Click the Email box and type `ada@`. A red ring and a short sentence appear under the box. Type `lovelace.org`: the ring
   turns green. Type `7` in Age: "Between 13 and 120". Type a short password, then repeat it differently: "That is not the
   same password".
5. Make the Game view taller, wider or a phone shape: the blue frame stays 16:9.
6. Stop Play. Select the `Price` object: the Inspector shows **UI Range Slider**. Select `Email`: **UI Input Field** and
   **UI Input Validator**. Select `Area`: **UI Aspect Ratio**. To add them to another scene: **GameObject > UI > Controls >
   Range Slider** or **Input Field with Validation**, **Layout & Overlays > Aspect Ratio Box**.

**What you should see.** A red ring and a sentence appear only after typing a wrong value; the range handles stop at each
other; the blue frame is 16:9 at every window shape. Pictures: a picture folder (pictures are no longer stored in the repository).

**Status.** Logic and the shipped scene are tested headless (`ui_library`, `ui_library_example`: 179 checks, run on Linux).
The pictures are software renders (`ui_library_shots`), not a real window. The editor menu entries and Inspector panels
were written but the editor was not built or run here, and nothing ran on Windows. The aspect box does not follow a
world-space Canvas yet.
