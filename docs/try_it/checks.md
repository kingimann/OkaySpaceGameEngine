---
id: checks
group: appendix
---
## Checks run for this page

Run on Linux, on the branch `claude/try-it`:

* `tests/test_project_create.cpp`, built on its own with g++ (it needs only the shared header), run from the source
  tree so the launcher and editor title cross-check really ran: **282 checks, 0 failures** (the same test on main:
  278). The test now expects 22 templates and that the editor's list equals the shared list, which includes
  "Vehicle (get in and drive)".
* `python3 scripts/gen_feature_map.py` then `--check`: up to date.
* The `examples` copy block of `scripts/build-windows-dist.sh`, run alone: 12 folders, 42 files, about 0.5 MB (472 KB on disk), no
  `.py`, `.cpp` or `__pycache__`. `bash -n` on the whole script.
* `okay::wav::DecodeWav` on `mulaw_tone.wav` and `Mesh::LoadOBJ` on the three OBJ samples, as described above.

**Not run:** `test_templates`, `vehicle_seat` and the other engine tests (the engine build was stopped; no engine
code changed). Compiling the editor and launcher (`EditorState::NewVehicleSeats3D`, the one-line launcher glyph change
and the new Console line in `NavigationWindow.cpp` were written by copying neighbouring code and were not compiled).
Anything on Windows. The NSIS installer and the zip.
