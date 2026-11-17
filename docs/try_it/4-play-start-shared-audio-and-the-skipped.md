---
id: 4
row: | 4 | Play start: shared audio, skipped navmesh bake | `examples\spatial_audio`, the Navigation window | Shipped; only the nav skip is visible |
---
## 4. Play start: shared audio and the skipped navmesh bake

Two speed-ups to Play start (`docs/audio.md`, `docs/navigation.md`).

**Audio sharing.** Sources that use the same sound file now hold one decoded copy. **There is no log line or other
sign of this in the editor**: it only saves memory and time. Open `examples\spatial_audio\spatial_audio.okayscene`
(seven sources on one `tone.wav`) and press Play: it should start without a pause, and playing a second time starts at
once. That is as far as you can see it. Honest gap: a "N sources share M clips" Console line would make this visible; it
does not exist yet. Tested on main: the headless tests (`play_start_bench` and the audio tests).

**Skipped navmesh bake.** A Nav Mesh Surface with **Bake before Play** on re-bakes when Play starts, unless nothing it
reads has changed since the last bake.
1. New Project > 3D Scene. Open **View > Editors & Tools > Navigation** and click **Add Nav Mesh Surface**.
2. Click **Bake** once. The Console says `Navigation: baked N polygons, ... in X ms -> <file>.okaynav`.
3. With **Bake before Play** ticked, press Play, then Stop, then Play again, touching nothing.
4. The Console now also says `Navigation: bake skipped, nothing the bake reads has changed since the last one`
   (this line is new in this change; the older build only repeated the "baked" line with the old numbers).
5. Move the floor or any collider, press Play: no "skipped" line, a fresh "baked" line.

**Status.** Unverified in the editor (the skip logic is covered by headless tests; the new Console line is a one-line
edit that was not compiled here).
