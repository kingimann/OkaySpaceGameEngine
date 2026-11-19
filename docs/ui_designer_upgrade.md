# UI designer upgrade (internal record)

Owner request: users should be able to build custom game UIs on a design canvas. Checked with
`scripts/find_existing.py` and by reading `docs/game_ui.md`, feature map 12.10 and `editor/main.cpp` (`DrawUIOverlay`).

| Wanted | Existing | Gap | Done here |
| --- | --- | --- | --- |
| Select, drag, resize handles | `DrawUIOverlay`: click, drag, handles | logic lives inside main.cpp, untestable | `Resize` (8 handles, Shift shape, Alt middle, minimum size) |
| Snap to grid / siblings / lines | grid and smart guides in the overlay | no draggable guides; no canvas-middle snap | `SnapMove` (guides, siblings, canvas, grid) |
| Nudge | arrows, Shift = grid step | in main.cpp | `Nudge` |
| Align, distribute, match size, stack, wrap | `Scene/UILayoutTools` and the Align menu | align to canvas for one widget | `Align`, `AlignTo`, `Distribute` |
| Z-order | hierarchy order only | no front / back buttons logic | `Reorder` |
| Layers (hide, lock, rename) | hierarchy panel | no lock; no hide that is design-only | `Layer`, `PickTopmost`, `PickInRect`, `UniqueName` |
| Zoom, pan, rulers | none | all | `View`, `RulerStep`, `RulerTicks` |
| Anchor picker | combo box (`anchorCombo`); UI plan P4 lists the picker as missing | visual 3x3 grid | `AnchorFromCell`, `CellOfAnchor`, `ReanchorKeepPlace` |
| Responsive preview | Game view fixed resolution | no list of aspect ratios, no off-screen warning | `PreviewSizes`, `PlaceAt` (uses `ResolveAnchorRect`), `OutsideCanvas` |
| Prefabs / shared styles | Save Selected as UI, Copy / Paste style | no variants, no shared styles | not done |
| Undo for every action | EditorState undo for UI moves | not audited | not done |

Written from general design-tool ideas and our own `UIAnchor` rules; no code from other projects.
Nothing new is saved in scenes (the model has no saved fields), so old scenes are unchanged and
`test_save_coverage` needs no new component.

Status: the model and its test (`ui_designer`) exist. The editor window does not call it yet (next step: replace the
matching code in `DrawUIOverlay`, add a rulers strip, a layers panel and a responsive size menu).
