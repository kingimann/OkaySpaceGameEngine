# Modeling side bar audit (branch `claude/modeling-sidebar-1`)

Internal record. The "side bar" is the page list on the left of the Modeling window (`editor/main.cpp`, child `##mdlnav`).
Its data and maths live in `editor/ModelingSidebar.hpp` (fold state, widths) and `editor/ModelingToolIndex.hpp` (pages by job,
search). Written from reading the code. Not run in a window (see the end).

| Area | Existing | Gap found | Done here |
|---|---|---|---|
| Grouping | Pages grouped by job (Start here, Make something, Change it, Fix parts, Extras, Files), plain titles, one-line hints | none | kept |
| Fold / narrow window | Chevron, Ctrl+\, auto-fold with 760/840 px hysteresis, saved choice | thresholds in raw pixels | scale with UI scale |
| Sizes at 125/150/200% | none: widths 196/36, rows 44/34, button 32, search box -90 were fixed pixels | text grows, boxes do not | all sizes take the UI scale; row height is never less than two text lines |
| Cut-off text | none: page name and hint were drawn with AddText and ran out of the row | clipped or overlapping at big scale or narrow width | `FitText` cuts with "..."; hover shows the full words |
| Greyed pages | Generic "Select a mesh object first." | not plain, no next step | `PageDisabledReason`: "Click a model in the scene first. No model yet? Open Add shapes and make one." |
| No model selected | page silently jumps to Easy | no explanation | a plain "Select a model to edit it" line with an "Add shapes" button |
| Active page | Highlighted Selectable | none | kept |
| Search | Find box with hit list and outlined buttons | none | kept |
| Scrolling | child scrolls by itself | not checked in a window | unverified |
| Per-tool items (extrude distance typing, cancel/confirm, shortcuts in tooltips, tool state on mode switch) | live in the Edit Mesh page and the Easy view, not the side bar | not audited here | not done |

Tests: `tests/test_modeling_sidebar.cpp` (scale, row height, FitText, reasons) failed to compile before the change and passes.

Unverified: `editor/main.cpp` was edited by hand and not compiled (the editor tree could not be built in this session);
nothing was seen in a window, so no screenshots were taken.
