---
id: reader-bench
---
## Model reader bench: our readers against Assimp (developer tool, Linux)

Not in the Windows release: it needs a build with `-DOKAY_USE_ASSIMP=ON` and Assimp's test files. On a Linux checkout:

1. Configure with `-DOKAY_USE_ASSIMP=ON -DOKAY_BUILD_TESTS=ON` and build `bench_model_readers`.
2. `OKAY_MODEL_CORPUS=<assimp v5.4.3 checkout>/test build/tests/bench_model_readers --format dae` prints one row per Collada
   file (our read time and peak heap against Assimp's, the full import with each) and a summary line saying "met" or "NOT
   met". Leave out `--format` for every reader (about half an hour).
3. `--digest` prints a hash of our output per file (it must not change when only speed work was done); `--compare` lists
   where our reader and Assimp disagree on counts and bounds.

What to expect and the latest numbers: `docs/own_readers_speed.md`.
