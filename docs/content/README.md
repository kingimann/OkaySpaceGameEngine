# content/: Getting models in

> **What this is:** the list of pages in `docs/content/`, one line each. Getting models in: model import, our own model readers, real-file tests, speed.
>
> **Status:** index, made 2026-11-20 when the docs were sorted into folders. Back to the [start page](../README.md).
>
> **Last checked:** 2026-11-20.

- [Real-file validation of the own model readers](model_corpus.md): Until this page the own readers had only been run on files we wrote ourselves.
- [Model import: who reads which format](model_import.md): **2026-11-17: the model readers are flipped.** The owner approved ("start switching them so i can test them out").
- [Our own model readers](model_readers.md): One section per model file format we read with our own code (written to replace Assimp). Speed, real-file tests and the formats still missing are in model_import.md, model_corpus.md, own_readers_speed.md and own_readers_remaining.md next to this page.
- [Model formats Assimp reads that we have no own reader for](own_readers_remaining.md): Goal (owner): no third-party dependency. Assimp v5.4.3 is the last big one on the model-import side.
- [Own model readers: speed and memory against Assimp](own_readers_speed.md): **2026-11-17: the model readers are flipped.** The owner approved ("start switching them so i can test them out").
- [Release note draft: model formats that stop importing when Assimp is removed](release_note_dropped_model_formats.md): Status: DRAFT. Nothing in this note has happened yet: Assimp is still in the build (off by default, on in the Windows distribution) and no default has been changed.
