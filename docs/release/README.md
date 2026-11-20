# release/: Getting builds to players

> **What this is:** the list of pages in `docs/release/`, one line each. Getting builds to players: build pipeline, packaging, releasing, channels, updater, install, content delivery.
>
> **Status:** index, made 2026-11-20 when the docs were sorted into folders. Back to the [start page](../README.md).
>
> **Last checked:** 2026-11-20.

- [The build pipeline](build_pipeline.md): How **File > Build Settings** (and the headless `OkayEngine --build-game`) turns a project into a game you can ship: what a build contains, every option and what it does, the build report, and what is not built yet.
- [Downloadable game content (content delivery)](content_delivery.md): An exported game can fetch new **data and scripts** from a server after it is installed: 3D models (characters, vehicles, buildings), textures and materials, animations, maps (a scene plus its assets), audio, dialogue, and Visual Script / O...
- [Install, update and uninstall](install.md): The install / uninstall / editor versions audit and plan, and how we compare with Steam and the Unity Hub.
- [The launcher's Versions page](launcher_versions.md): The launcher keeps other versions of the editor next to the one you are running, so every project can be opened with the version it was made with.
- [Packaging & distribution layout](packaging.md): The Windows distribution is laid out so a user double-clicks one thing at the top and everything else is tucked away.
- [Release channels: latest and LTS](release_channels.md): OkaySpace ships on two channels.
- [Releasing](releasing.md): How an update is released: the normal path and the one-dispatch fast release.
- [Releasing on Steam](steam_release.md): OkaySpace has Steam built in via `ISteamService` (achievements, stats, rich presence, identity).
- [The updater: integrity, self-update, rollback](updater.md): How the launcher (and the editor) replace their own files, what is checked before anything is replaced, what the person sees when it does not work, and what is still open.
