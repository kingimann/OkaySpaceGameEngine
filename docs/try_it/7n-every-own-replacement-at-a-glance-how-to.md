---
id: 7n
group: switches
row: | 7n | Every own replacement in one table: the switch, the sample, how to tell it is on, and what is missing | section 7n below and `docs/status/own_flip_proposal.md` | Table is new. Read from the code and from CI logs, not run on Windows |
---
### 7n. Every own replacement at a glance: how to try each, and what is missing

This is the one table for everything that is built and switched off. It is the same table as section 5 of `docs/status/own_flip_proposal.md`,
which also says how ready each item is, in what order they could become the default, and what blocks deleting the library each one replaces
(CLAUDE.md rules 4a and 17). Set a switch the way the section "Switches that are off by default" says: an environment variable in the
Command Prompt, then start the program from that window. The sure way to see which parts are ours is **Help > About OkaySpace** in the editor.

Checked by reading the code and the CI logs on 2026-11-15. Nothing here was run on Windows.

| Item | The switch | A sample that ships in the download | How you can tell it is on | Gap |
|---|---|---|---|---|
| FBX | `OKAY_OWN_FBX=1` | **none.** Use any `.fbx` you own | About popup: `readers=own(fbx,...)`, and the word `ufbx` goes away | no sample |
| COLLADA | `OKAY_OWN_COLLADA=1` | `examples\own_readers\skinned_strip.dae` | `readers=own(dae)` | none |
| 3DS | `OKAY_OWN_3DS=1` | `examples\own_readers\cube.3ds` | `readers=own(3ds)` | none |
| OFF | `OKAY_OWN_OFF=1` | `examples\own_readers\cube.off` | `readers=own(off)` | none |
| BVH, SMD, MD5, MS3D, ASE, Blender | `OKAY_OWN_<FMT>=1` | **none** | `readers=own(<fmt>)` | no sample |
| 3MF, AMF, X3D, DXF, LWO, MD2, MD3, IRRMESH, B3D, X | `OKAY_OWN_<FMT>=1` | **none** | `readers=own(<fmt>)` | no sample |
| AC3D, OGRE, XGL, IRR, NFF, TER, MDC, HMP, CSM, Q3D, RAW, IQM, COB, NDO, PMX, LWS | `OKAY_OWN_<FMT>=1` | **none** | **nothing**: they are not in the stack line and no log line says which reader ran | no sample and no way to tell |
| OGRE binary, ZAE, `.xml` (gap batch) | `OKAY_OWN_OGREBIN=1`, `OKAY_OWN_ZAE=1`, `OKAY_OWN_XML=1` | `examples\own_readers\cube.mesh`, `cube.zae`, `cube_irrmesh.xml` | **nothing** (same as the row above) | no way to tell |
| STL and PLY first | build option `OKAY_PREFER_OWN_READERS` only | none | nothing | no run-time switch, no sample, no sign |
| WAV | `OKAY_OWN_WAV=1` (`0` = old) | `examples\own_wav\own_wav_test.okayscene` with `mulaw_tone.wav` | you hear it: old = buzzy, own = a clean 440 Hz tone | no startup word, no log line |
| Own HTTP | Linux builds only: `OKAY_OWN_HTTP=1` | none | `http=own` (Linux) | cannot be tried on the Windows download |
| Own platform layer | `OKAY_PLATFORM_BACKEND=own`, then `Tools\OkaySpacePlayer.exe` | any exported game | `platform=own` | unverified on Windows; editor and launcher still use SDL windows |
| OkayUI | `OKAY_UI_BACKEND=okayui` | none needed | About popup `ui=okayui` | unverified in the Windows build |
| CodeEdit (script editor) | `OKAY_UI_BACKEND=okayui` and `OKAY_DEV=1` | any script | none | never run in a real window |
| okay::crypto | none, on purpose | none | the line says `crypto=libsodium` | nothing to try |

The gaps (no sample for 32 of the 35 reader switches, no way to tell that the 16 newest readers or WAV are on, no run-time switch for STL
and PLY, no import log line) are "Step 0" of the proposal. Until they are closed, a PR that adds or changes one of these must say so
at the top (rule 4a).
