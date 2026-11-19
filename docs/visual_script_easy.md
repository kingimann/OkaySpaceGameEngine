# Visual Script: easy for beginners (internal record)

Owner decision: Visual Script must be easy to understand for beginners; text scripting is for people who already
know it. This is the Existing / Gap record for that work (not a guide). Checked with
`python3 scripts/find_existing.py visual script beginner tutorial template` and `docs/feature_map/14-visual-scripting.md`.

| # | Wanted | Existing (before this branch) | Gap | Status |
|---|--------|-------------------------------|-----|--------|
| 1 | "Start here" empty-graph screen with goal buttons | `VisualScriptStart.hpp` + `VisualScriptStartDraw.hpp`: recipe cards on an empty graph, recipes inserted wired (`VisualScriptRecipes.hpp`, run headless in `vs_recipes`) | No recipe for "Show a score" or "Wait then do something"; cards were titled with recipe names, not goals | Added both recipes (headless-run tests) and goal titles/order (`kVsStartGoals`) |
| 2 | Recipes from right-click | Recipes menu in the toolbar and in the add menu (`VsInsertRecipe`) | none found | Existing |
| 3 | Plain verbs, Beginner/Advanced palette, synonym search | `VisualScriptPlain*.hpp`, `VisualScriptAdvanced.hpp` (beginner list of ~55, "Show all nodes" remembered), `VisualScriptSearch.hpp` (synonym groups) | none found beyond list tuning | Existing |
| 4 | Live feedback, plain error list with Fix | `VisualScriptWhatHappened.hpp`, node flash and live values (`BlockExecCount`, `execCount`), problems list with `VsFixIssue` | none found | Existing |
| 5 | Wire help, converter node | `VisualScriptPlainWire.hpp`, `VisualScriptChoices.hpp` (`VsOfferConvert`), `VsFixMismatch` | greyed sockets while dragging not re-checked here | Existing / not re-checked |
| 6 | Explain this graph | nothing | pure sentence from the blocks | Added `VsExplainGraph` (`VisualScriptExplain.hpp`), shown in the new panel |
| 7 | "Why isn't it working?" checklist with Fix | "Nothing runs yet" hint, problems list | no checklist for not attached, no start block, loose block, empty needed input, start block never ran | Added `VsWhyNotWorking` / `VsApplyWhyFix` and a canvas button + panel |
| 8 | Templates per game type wired to Visual Script | `VisualScriptTemplates.hpp` (many templates), `VsSampleGame()` | not extended | Not done |

Written from this project's own graph rules; no outside code. New saved data: none (the summary and checklist are
computed; nothing is written to `.okayvs`). Tests: `vs_explain`, `vs_recipes`, `vs_start`.
