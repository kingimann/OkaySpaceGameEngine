# process/: How the work is done

> **What this is:** the list of pages in `docs/process/`, one line each. How the work is done: the two Claudes, agent briefs, reviews, testing and QA.
>
> **Status:** index, made 2026-11-20 when the docs were sorted into folders. Back to the [start page](../README.md).
>
> **Last checked:** 2026-11-20.

- [Agent brief template](agent_brief_template.md): Agent brief template
- [Automated testing](automated_testing.md): Feature map area 33. Three pieces, all in `okay::testing` (headers under `okay/Testing/`, sources in `engine/src/Testing/`, part of `okay_engine`):
- [How the two Claudes work together](collaboration.md): Two Claudes work on this repo at the same time: **Claude 2** (branches `claude2/*`, coordination issue #422; bug fixes and third-party replacement) and the **coordinator** with its agents (branches `claude/*`; unfinished features, reviews, releases).
- [Developer + QA sessions working together](qa_workflow.md): Two Claude Code sessions work on this repository at the same time:
- [okay_run](okay_run.md): run a scene headless with scripted input, trace objects and the camera per frame, take pictures and pass/fail checks.
- [Review log](review_log.md): What has been reviewed, by whom, what was found and where each fix stands.
- [Every template and example is tested](templates_check.md): Owner rule (2026-11-15): "make sure all templates you make for anything works." Two tests now run every project template and every example scene we ship, the way a person would, and fail when one is broken.
