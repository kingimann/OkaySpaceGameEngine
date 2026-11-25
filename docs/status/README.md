# status/: What is going on and what is left

> **What this is:** the list of pages in `docs/status/`, one line each. What is going on and what is left: the work board, unfinished lists, the own-code roadmap, audits of where code came from.
>
> **Status:** index, made 2026-11-20 when the docs were sorted into folders. Back to the [start page](../README.md).
>
> **Last checked:** 2026-11-20.

- [What is custom and what we build on](THIRD_PARTY.md): OkaySpace is mostly our own code. This page lists what we wrote, and the outside libraries and services we build on, with their licences.
- [Unfinished features](UNFINISHED.md): everything that is not finished (hand-written).
- [Roadmap: replacing the third-party libraries with our own code](own_code_roadmap.md): **Goal: removal.** Every third-party library in the build is to be replaced by our own code and then deleted from the repository, slowly.
- [Foundations: what we have, what runs, what is missing](foundations_gap.md): audit of the 20 engine foundations against Unity, Unreal and Godot, with the build order (2026-11-21).
- [Own replacements: what can become the default, in what order, and what blocks deleting each library](own_flip_proposal.md): **2026-11-17: the model readers are flipped.** The owner approved ("start switching them so i can test them out").
- [Own stack rollout: connecting our own code to the real programs, step by step](own_stack_rollout.md): **2026-11-17: the model readers are flipped.** The owner approved ("start switching them so i can test them out").
- [Modeling tab audit: what a modeller expects, what we have (branch `claude2/modeling-tab-2`)](modeling_tab_audit.md): the Edit Mesh checklist (have / missing / broken), the bugs fixed and the tools added on 2026-11-22.
- [Player audit: bugs, frame cost and what was done (branch `claude/player-deep`)](player_audit.md): The owner asked: "look into it deeper: the physics, the player, how the terrain works, and fix bugs, fps problem and improve".
- [Provenance audit: code that may have been copied or ported](provenance_audit.md): Date: 2026-11-14. Branch `claude/provenance-audit`, cut from main at `ff89d98a`.
- [Engine-wide unfinished audit (new findings)](unfinished_engine.md): Base: origin/main 13a892ec. Scope: engine/, editor/, player/, shared/ (launcher excluded).
- [Launcher: what is unfinished (plain-English to-do list)](unfinished_launcher.md): Read from origin/main 13a892ec (clean read-only tree).
- [Work board](work_board.md): a hand-written snapshot of what the main session and its agents are doing, the open PRs, the releases and what waits on the owner.
