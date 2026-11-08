# Feature map pages

`docs/FEATURE_MAP.md` is generated from the pages in this folder by `scripts/gen_feature_map.py`.
Edit a page, then run the script. `scripts/check_feature_map.py --list` lists source files that no page
mentions.

## Page layout
```
# <Area number>. <Area name>

<2-4 lines: what the area is, which parts of the repo it covers>

## <Area number>. <Area name>

### 5.6 Foot, limb and chain IK
- **What:** one or two plain sentences.
- **Parts:**
  - 5.6.1 Two-bone foot IK - `Animation/FootIK` - what it does in one line
- **Code:** `Animation/FootIK`, `editor/AnimatorWindow.*`
- **API:** classes, components, script builtins, menu items an author would type
- **Editor:** window, menu or inspector, or "none"
- **Docs:** `docs/animation.md`, or "-"
- **Tests:** ctest names (the part after `test_`)
- **Status:** Works | Partial | Basic | Planned | In flight - then one line why
- **Open:** semicolon-separated: missing, known bugs, in-flight PRs
- **Related:** sub-area IDs, e.g. 5.7, 6.6
- **Words:** comma-separated terms people use (class names, menu names, synonyms)
- **Seen as:** semicolon-separated things you see when it is wrong or when you want it
- **Ask me like:** "example request"
```
One page per area, named `NN-slug.md`. A page may hold several areas (`20-35-planned.md`). Files whose
name starts with `_` (`_header.md`, `_footer.md`) are the hand-written text around the generated
tables. Paths in `Code:` are relative to `engine/include/okay/` without extension for engine headers
(`Animation/FootIK` covers the .hpp and the matching .cpp) and repo paths for everything else.
Required lines: What, Code, Status, Words. Part IDs are `N.M.K` and unique inside their sub-area.
