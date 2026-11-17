---
id: 7zr
row: | 7zr | A prefab variant cut in the middle of a line is refused ("cut short"), not loaded with a record missing; deep project folders work | Drag a damaged `.okayprefab` variant into the scene in `Tools\OkayEngine.exe` | New in this change; see section 7zr |
---
## 7zr. Damaged prefab variants and deep project folders

What changed: a variant prefab file that was cut off in the middle of its last line (a crash or a full disk while
saving, a bad copy) used to load anyway, quietly missing the half-written record (for example a colour or tag
override). It is now refused with an error that says the file is cut short, and the scene is left as it was.

1. In the editor, make a prefab, then **Create Variant** of it, and change something on the variant (a tag) so the
   variant file has an `override` line.
2. Close the editor. Open the variant `.okayprefab` in Notepad. Delete the end of the file from the middle of an
   `override ...` line onwards (keep `override 2 ` and delete the rest), and save.
3. Start the editor and drag the variant into the scene. You should see an error that names the variant and says
   "cut short", and nothing is added. Before this change the variant appeared without that override.
4. Deep folders: put the project in a folder whose full path is longer than 260 characters and repeat steps 1 and 3
   with an undamaged variant. It should load normally.

Also fixed for deep folders on Windows (code only): a file dragged onto the editor window from a deep folder kept
only the first 520 characters of its path; the editor's own path (used to find the player, plugins, downloads and
crash reports) was cut at 260 characters; the crash report folder was cut at 260 characters.

**Checks run for this change** (Linux): `prefab_variants` passed with 201 different temp folder path lengths
(about 100 to 300 characters); its new suite cuts a variant in a folder over 300 characters deep at every byte, and
failed before the fix (82 cuts in the middle of a line loaded). `ctest -R "prefab|scene|asset|path"`: see the PR.

**Not seen:** the editor steps above were not run; the Windows changes (drag and drop, editor path, crash folder,
`Backend::BasePath`) were only syntax-checked with MinGW, not built or run on Windows.
