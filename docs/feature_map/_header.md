# OkaySpace feature map

Point at anything and I know where it lives. Say **"work on 5.6.2"** (one numbered part),
**"work on 5.6"** (a sub-area) or **"work on area 5"** (everything in it), or just describe what you
see ("feet slide on slopes", "the Hierarchy window"). Every number leads to the files, classes, tests,
docs and editor windows involved, what already works, what is missing, and what is in flight.

This map holds {{STATS}}, and every source file in the repo is meant to appear in it
(`python3 scripts/check_feature_map.py --list` shows any that do not).

**How it is organised**
- This file is the overview: the areas and one summary table per area. It is generated; do not edit it
  by hand. Two lookup pages are generated next to it: [By what you see](feature_map/_index_symptoms.md)
  and the [A-Z index](feature_map/_index_az.md).
- `docs/feature_map/NN-*.md` are the detail pages, one per area. Each sub-area has a block with what it
  is, its numbered parts, code, API, editor window, docs, tests, status, what is open, related
  sub-areas, the words people use for it, what you see when it is wrong, and an example request.
- To change the map: edit a page, then run `python3 scripts/gen_feature_map.py`. The format is in
  `docs/feature_map/README.md`.

**Status words:** Works (implemented and exercised by tests or shipping features), Partial (works with
known gaps), Basic (minimal version), In flight (a PR or branch exists, not on main), Planned (not built).
"Open" lists come from the code, ROADMAP.md, and open PRs; treat an open item as "ask before assuming it
is still open". Paths are under `engine/include/okay/` unless they start with `editor/`, `launcher/`,
`docs/`, `scripts/` and so on. Components live in `Components/`.

## How to point at something

Four ways, use whichever is easiest:

1. **By number:** "fix 4.2", "work on 12.4 and 12.3", "do 5.6.2".
2. **By what you see:** find the window, page or symptom in [By what you see](feature_map/_index_symptoms.md).
3. **By word:** look the word up in the [A-Z index](feature_map/_index_az.md). If a word is not there,
   say it anyway and I will search the code and tell you which number it is.
4. **By file or class:** say the name; the detail pages list the files and classes of every sub-area.

You can also say "area 5 but not Grok's parts" or "everything open in 1".
