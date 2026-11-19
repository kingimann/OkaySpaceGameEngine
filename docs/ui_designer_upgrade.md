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

Second step (branch claude/ui-designer-2), found while wiring: the UI-only view ALREADY has mouse-wheel zoom about the
cursor, middle / Space drag pan, a resolution and aspect dropdown (`kGameResPresets`), a red outline for widgets that run
off screen, a safe area, a subdivision grid, and a popup anchor picker (3x3 plus stretches) in the Inspector. These were
not rebuilt (no second copy). What was added, in `editor/UIDesignerWindow.*` with small hooks in `main.cpp`:
- rulers (design pixels, tick steps from `uid::RulerTicks`), draggable guides (drag from a ruler, drag back to delete)
  that the widget drag snaps to, a Fit button and a zoom percentage;
- a UI Layers panel (corner "L" button): front-first tree, eye (uses the object's saved Active flag, one undo step),
  lock (blocks canvas click and box select), double-click rename, drag to reorder or drop in the middle to nest.
Guides and locks are session-only and write nothing to scenes, so `test_save_coverage` is unchanged.
The old overlay snap / resize code in `DrawUIOverlay` / `EditUIWidgets` was NOT moved onto `UIDesignerModel`: it is
behaviour-rich and cannot be run headless here, so replacing it blind risked regressions. The model's `SnapMove` and
`Resize` are the tested reference for a later swap.

Status: the model and its test (`ui_designer`) exist. The editor window does not call it yet (next step: replace the
matching code in `DrawUIOverlay`, add a rulers strip, a layers panel and a responsive size menu).

## Third step (branch claude/ui-designer-3)

| Item | Result |
| --- | --- |
| Snap and resize maths onto the model | `SnapEdge`, `BestSnap`, `ParentEdgeSnap`, `ConstrainResize` moved out of `EditUIWidgets` unchanged; `test_ui_designer` replays 400 random cases and every handle with Shift / Alt against verbatim copies of the old code. The inline pixel clamping, grid rounding and child scaling stay in main.cpp. |
| Shared styles | New `UIUserStyle` component (a named style object) and `UIStyleVariant::userStyle`; `ApplyUserStyles` restyles every widget that names a style. Saved as `uiuserstyle` records and `vuser`, only when used. Listed in `test_save_coverage`. Panel: Layers window > Shared styles. Test: `ui_user_styles`. There is no runtime re-apply: values are copied onto the widgets when the style is edited or used, so saved scenes carry them. |
| UI prefab variants | Already built (`PrefabTools`, `editor/PrefabEdit`: instances, overrides, variants, apply, update all). Not rebuilt. Added plain-word buttons in Layers window > Reusable UI that call it. No new logic, so no new test. |
| Image asset picker with thumbnails | Already there: UI Image uses `AssetSlot` with a thumbnail and Set Native Size. Not rebuilt. |
| Guides saved per scene | New `UIGuides` component, created only by "Keep guides with the scene"; scenes without it write nothing. Test: `ui_user_styles` (saved guides). |
| Undo audit | Audited `EditUIWidgets` and the new panel: move, resize, anchor pip, rename, hide, reorder, styles, guides toggle all call `PushUndo` first. Found one gap: arrow-key nudge had no undo (and moved only one widget). Fixed in `editor/UIDesignerActions.hpp` (`NudgeSelection`, one undo step, all selected, skips locked); test `ui_designer_actions`. Dragging a guide is session-only and has no undo by design. |
