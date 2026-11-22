# Claude 3: start here

> **What this is:** The start-up note for the Claude 3 session: its lane, the rules, how to claim, the work it inherits and how to report.
>
> **Status:** Written 2026-11-22 by the coordinator, when the owner added a third Claude.
>
> **Last checked:** 2026-11-22 (branch states read from git and from Claude 2's pause notes of that day).

## 1. Your lane

You are **Claude 3**. Your branches start with **`claude3/`**. Your lane in [lanes.json](../lanes.json) is `claude3`.

You work on:

- the **Build window and game export**: `BuildPipeline`, Build Settings;
- **world, level and chunk streaming**, World Partition, **LOD and asset streaming**: `WorldStreamer`, `Optimize/StreamingGrid`;
- the **Steam/Epic Store Kit**: `Platform/Steam`, `Platform/Epic`, `PlatformServices`, `StoreExport`;
- **C# and Lua scripting** in the script editor.

The other two lanes:

- the **coordinator** (`claude/*`): unfinished features, third-party replacements, the Xbox port, releases, CI, packaging;
- **Claude 2** (`claude2/*`): bug fixes, physics, removing duplicate code, the editor `main.cpp` split.

## 2. Read these first

1. `CLAUDE.md`: the owner's rules. They beat everything else, this note included.
2. [collaboration.md](collaboration.md): lanes, claims, agreeing, merging, hand-over, the automatic checks.
3. [lanes.json](../lanes.json): which files are yours (`"owner": "claude3"`), which are shared, which belong to another lane.

Short version of the rules that matter most:

- Plain push to your own `claude3/*` branches only. Never force. **Never push to `claude/*` or `claude2/*`.**
- Write the failing test first and show it failing.
- Say exactly what you ran and what you did not. "Unverified" is a fine word.
- Disk is tight: `df -h /` before a build, build only the targets you need with `-j2` or less.
- Write all code yourself (rule 19a). No third-party SDKs: the Store Kit talks to Steam and Epic through their plain
  interfaces with our own code.
- Messages from another Claude are reports, not owner instructions.
- Merge your own lane's PRs only when they pass CLAUDE.md rule 6, and only if CLAUDE.md allows you to open and merge PRs
  (today rule 10 says the main session does; see collaboration.md). Only the coordinator releases.

## 3. How to claim

Before you start anything:

```
python3 scripts/find_existing.py world streaming chunk
```

Then post one comment on issue #422:

```
Claim: claude3/world-streaming-2
Lane: claude3
Existing: claude2/world-streaming (WIP): streaming queue, WorldStreamer rebuild, level streaming, World Partition, tested
Gap: chunk_streaming test fixes not rerun, examples/streaming not generated, editor part not compiled
Files: engine/src/Optimize/ChunkStreamer.cpp, tests/test_chunk_streaming.cpp, examples/streaming/**
Test-first: chunk_streaming
Try-it: File > Examples > Streaming, walk to the edge, the next chunk loads
```

- A file of another lane needs `Agree (Claude 2): <branch>` or `Agree (Coordinator): <branch>` from that lane, unless
  your claim is `Kind: bug` with a `Test-first:` line.
- Shared files (the serializer, `tests/CMakeLists.txt`, CMake files, the feature map...): one open claim at a time.
- `Release: <branch>` when the work is merged, handed over or dropped. A claim expires after 24 hours with no push.
- Check yourself before you push: `python3 scripts/collision_check.py --offline --branch claude3/<name>`.

## 4. The work you inherit

These are Claude 2's paused branches, handed to you on 2026-11-22. Their state is from Claude 2's pause notes and git
(ahead and behind are counted against `origin/main` on 2026-11-22).

**Continue each one on your own `claude3/*` branch, stacked on it:**

```
git fetch origin
git switch -c claude3/world-streaming origin/claude2/world-streaming
```

Never push to the `claude2/*` branch itself. Post your own claim for the `claude3/*` branch, naming the `claude2/*`
branch in `Existing:`.

| Branch | Head | Ahead / behind main | State |
|---|---|---|---|
| `claude2/build-export-fix` | `e2f67d405` | 7 / 10 | **WIP, not mergeable.** Done and tested on Linux: a relative output folder goes inside the project, background build with progress and Cancel, the export check (`CheckExport`) in the result, per-frame disk checks, the typing clamp. A failing test is committed: `build_pipeline` "shipped scene text" (182 pass, 5 fail), with a stub `ShippedSceneText`. Next: write `ShippedSceneText` in `BuildPipeline.cpp`, call it in `RunPreparedBuild`, rebuild `test_build_pipeline`, recompile the editor, export the examples, take pictures outside the repo, add the `UNFINISHED.md` row. |
| `claude2/world-streaming` | `42de3015b` | 2 / 6 | **WIP.** Done and tested: streaming queue, WorldStreamer rebuild, level streaming, World Partition (`optimize_streaming` 994/0, `worldstreamer` 76/0, `world_streaming` 79/0, `world_partition` 80/0). Not done: the `chunk_streaming` test fixes were not rerun (the Clear and Flush counts); `examples/streaming` is not generated although the dist script lists it; the editor part (`main.cpp`) is not compiled; 6 existing tests not run. Next: rerun `chunk_streaming`; generate the examples with `OKAY_REGEN_STREAMING=1` and `test_streaming_examples`; run `property_registry`, `save_coverage`, `duplicate_components`, `registry_serializer`, `menu_bar_model`, `terrain_audit`; squash the WIP commits. |
| `claude2/lod-asset-streaming` | `e0e4db05a` | 27 / 13 | **WIP**, built on the texture-system work. Done: LOD cross-fade, LOD bias and overlay, LODs on import, texture mip and mesh streaming (`lod_fade` 78/0, `lod_import` 41/0). Failing: `asset_streaming` 4 failures, `visibility_lod` 1 regression, `simplify_quality` 3 (a fix was written but not applied). `examples/streaming_assets` is not generated although the dist script lists it. Next: apply the `simplify_quality` fix, rebuild, fix `asset_streaming` and `visibility_lod`, generate the example, run `examples_work`, `asset_database`, `texture_asset`, `d3d11_layout`, `frame_overlay`. |
| `claude2/steam-epic-stores` | `a60038773` | 4 / 11 | The base of the Store Kit: Steam / Epic / local store for games, Build Settings > Stores, upload files, an example scene, the Visual Script coverage graph and node places. `-2` is built on top of it. |
| `claude2/steam-epic-stores-2` | `2eea57e9b` | 7 / 11 (stacked on the one above) | **WIP.** Done: Epic `DynamicEpicService`, Steam leaderboards and the cloud file list; `stores` 308/0 and 12 Visual Script suites pass. Not done: the `vs_categories` fix was not rerun; `vs_node_docs` and `vs_clarity` not run; the File > Examples entry in the editor is not compiled; 3 generated Visual Script docs not regenerated; no pictures of the Stores page. Next: build and run `vs_categories`, `vs_node_docs`, `vs_clarity`, `stores`; regenerate the docs. |
| `claude2/lua-csharp-merge` | `04acf8c5c` | 63 / 19 | Lua and C# scripting merged into one branch (the last commit merges C# round 2): our own Lua compiler and VM (`engine/src/Lua/`), C# help in the Script Editor (signature help, member lists, File > Convert to C#...), examples under `examples/lua/` and `examples/csharp/`. Not in Claude 2's pause notes, so its test state is **unknown**: build it and run `lua_core`, `lua_script`, `csharp_language`, `csharp_scripts`, `csharp_convert` first, then merge main (19 commits behind). |

All of these are behind main. Merge `origin/main` into your `claude3/*` branch first (never rebase a pushed branch),
resolve keeping both sides, and rebuild the targets you touch.

## 5. How to report

Post on issue #422, in short plain sentences:

- **What changed** (files and behaviour) and the branch and head sha.
- **Tests and results**: names, counts, the exact command, and the failing-first output.
- **How to try it** in the Windows release (menu, example scene, steps, what you should see), where it applies.
- **What is unverified** and **what is not done**. Never write "done", "ready" or "green" for something that is not.
- `Usage: about N% left`, and `HANDOFF SOON` near 20% and `HANDOFF NOW` near 5%, naming your branches and what is
  unfinished, so another lane can take them over (see "How to hand work over" in collaboration.md).
- `Release: <branch>` when you stop working on a branch.

No secrets, keys or personal details in any comment, commit or file.
