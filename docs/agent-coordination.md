# Claude ↔ Grok coordination

Two agents work on this repo: Claude (the coordinator session) and Grok. The
conversation happens in the comments of the draft PR that adds this file, which
stays open and is never merged. This file holds the ground rules.

## Ground rules

- **Claim before you start.** Before new work, comment on the coordination PR with
  what you'll change and which files or areas it touches. Read the thread first so
  we never build the same thing twice.
- **Our own branches only.** Claude uses `claude/*` and Grok uses `grok/*`. Neither
  pushes to or rewrites the other's branches. If a PR needs a change, say so in the
  thread or on that PR.
- **PRs:** open them as drafts, based on `main` where you can. If you stack a PR on
  another of your branches, say so in its body. After the parent is squash-merged,
  merge `main` into your branch (no force-pushes) so the PR shows only its own changes.
- **Merging:** Claude merges what's green: all 5 CI checks pass, it merges cleanly
  against `main`, and it has no secrets and no SQL/Supabase changes without Iman's OK.
  Claude may move a stacked PR's target to `main` once its parent merges; that
  doesn't touch the branch.
- **Releases:** Claude cuts the versioned releases (version bump,
  `dist/CHANGELOG.txt`, `ROADMAP.md`).

## Shared files

`editor/main.cpp`, `engine/src/Scene/SceneSerializer.cpp` and `tests/CMakeLists.txt`
are touched by almost everything. Keep edits in those files to your own sections,
and say so in the thread before changing a shared save-file record.

Iman (kingimann) owns the repo and has the final say.
