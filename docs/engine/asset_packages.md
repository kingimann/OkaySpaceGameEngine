# Asset packages (`.okaypkg`)

Internal record: the audit of what existed, the gaps against the owner's request of 2026-11-26, the design, what round 1
built, the benchmark numbers and what is left. System name: **Asset Packages** (feature map 1.21). Code:
`engine/include/okay/Core/AssetPackage.hpp`, `engine/src/Core/AssetPackage.cpp`, `engine/src/Core/AssetPackageFolder.cpp`.
Tests: `tests/test_asset_package.cpp`, `tests/test_asset_package_bench.cpp`.

**Written from:** first principles and the engine's own code. No other engine's package or archive layout was read or
imitated (not Rockstar's, not any other); no outside code, library or tool was used, not even to compare results.
Compression is the engine's own DEFLATE pair (`ImageWrite::Deflate`, `inflate::Raw`, written earlier from RFC 1951).
The hash is new and ours (see "Hash64").

## 1. Audit: what existed

| Existing | What it does | Relation to asset packages |
|---|---|---|
| Web pack `game.okaypack.gz` (`OKWPACK1`): `scripts/web_package.py` writes it, `platform/web/okay_web.js` reads it in the web player, `engine/src/Browser/GamePack.cpp` reads it in the browser | One download for a web game: 8-byte magic, a JSON list of `{path, offset, size}`, the files, the whole thing gzipped | **A different job: transport.** It carries a folder over HTTP to the web player or the browser, which unpack it into a folder. No ids, no per-file hash, no per-file compression, no patches. Not changed (its readers are the web player and the browser lane's files). When the web export starts packing (round 2+), the `.okaypkg` files travel *inside* it like any other file; the two formats sit on different layers and do not overlap. |
| `Core/DataPack` (`OKP1`) | Scrambles one scene or config file (XOR keystream + FNV-1a) so it cannot be read in a text editor | Per-file obfuscation, kept. An encrypted build's scenes go into the package as their `OKP1` bytes; the player unscrambles them as before. |
| `.okaymeta` GUIDs, `Core/AssetDatabase` | 128-bit random GUID per asset (or one derived from the project path with `StringId` hashes when no meta exists), import settings, dependency graph (`AssetDatabaseDeps`: scene, material, text and model reference scanners) | GUIDs are editor identity; they are random, so not usable as deterministic ids. Package ids come from the exported file name (deterministic, 64-bit). The dependency scanners are **reused** by `PackFolder` to fill the package's dependency lists. |
| `Core/AssetManager` | Typed handles, one cache entry per path, background loads on `EngineJobs()`, hot reload, `Stream<T>` with priorities and a RAM budget (asset streaming round 1, `StreamSettings`) | **Extended**: `MountPackages`, `ReadBytes`, the `AssetBytes` loader (so `Get`/`LoadAsync`/`Stream` work on packaged files) and `StreamWithDependencies`. |
| `Core/BuildPipeline` (Claude 3's lane) + `editor/BuildGame.cpp` | The dependency walk, the layouts (`<Name>_Data/`, `Data/`, zip), the report, `build.json`, the zip | **Extended in `BuildGame.cpp` only**: after the data folder is complete it is packed. `BuildPipeline.*` is not touched. |
| Content delivery (`docs/release/content_delivery.md`, `okaycontent::`) | Off by default; a signed text manifest of files with SHA-256, downloads changed files into an overlay folder | The per-file update path. The package manifest (`packages.manifest`) is the per-package equivalent; wiring the downloader to it is round 2 (it should reuse content delivery's signed transport, not a second downloader). |
| `Core/StringId` (FNV-1a 64) | String hash for ids in code | Byte-at-a-time and weak on long inputs; fine for names, slow for content. Not used for packages; `Hash64` was written (one hash for ids and content). |
| `ImageWrite::Crc32`, `Crypto/Sha256` | CRC-32 for PNG/zip; SHA-256 for signatures | CRC-32 is 32 bits (too small for ids); SHA-256 is cryptographic and slower. Neither replaced. |
| Editor names | The Project panel shows project paths from the asset database | Packages keep a name table (on by default), so tools and the editor can show `Assets/rock.png` instead of an id. |

## 2. Gaps against the request, and what round 1 did

| Owner's bullet | Before | Round 1 |
|---|---|---|
| Own versioned binary package format | None (only the web transport pack) | Done: `.okaypkg` version 1 (layout below) |
| Deterministic 64-bit ids, collision detection | GUIDs (random, 128-bit) | Done: `IdOf(name)`; two names with one id (a collision or a case variant) is a hard error naming both, at build and at read |
| Readable names in the editor | n/a | Name table kept by default; `WriteOptions::keepNames = false` strips it. No package browser window yet (round 2) |
| Package and hash automatically on export | Loose file copy | Done for Build Game (desktop): `game.okaypkg` + `packages.manifest`; web/Android exports still loose (round 2) |
| Compression | Zip of the whole build only | Done: per-entry DEFLATE, kept only when it saves at least 1/32; already-compressed formats skipped |
| Asynchronous loading | AssetManager jobs (files only) | Done: `PackageStack::ReadAsync` on the job system; `AssetManager` async/stream loads of packaged files through `AssetBytes` |
| Asset streaming | `Stream<T>` by file | Packaged files stream through the same `Stream<T>`; streaming *inside* one large entry by chunk is round 2 |
| Dependency tracking | Editor-side graph only | Done: dependency ids per entry, `Closure` (dependencies first), `AssetManager::StreamWithDependencies` |
| Incremental updates without rebuilding | None | Done in the core: patch packages (`MakePatch`, replace/add/remove by id, chained to the exact base), `packages.manifest` + `ChangedPackages`, and a compression cache so a re-export compresses only changed files. Making a patch from the editor and the downloader are round 2 |
| Integrated in engine, editor, build, runtime | n/a | Engine (AssetManager), editor export (BuildGame), player (mount before anything loads); CMake lists the files |
| No extra configuration or services | n/a | Nothing to set up; on by default; `OKAY_BUILD_LOOSE_FILES=1` only to go back |
| Tests, benchmarks, corrupted-package validation | n/a | `asset_package` (297 checks; 237 in the standalone fuzz build), fuzzing under ASan/UBSan, `asset_package_bench` |

## 3. Design

### File layout (version 1, little-endian)

```
offset 0    header      96 bytes
              0  magic "OKAYPKG\0"          8  u16 version (1)       10 u16 header size (96)   12 u32 flags (1 names, 2 patch)
             16  u32 entry count           20  u32 record size (64) 24 u32 alignment (power of two, 1..65536)
             28  u32 reserved (0)          32  u64 data offset (96) 40 u64 data bytes
             48  u64 table offset          56  u64 names offset     64 u64 names bytes
             72  u64 deps offset           80  u64 deps bytes       88 u64 base stack hash (0 = a base package)
96          data        each entry's bytes at an `alignment` boundary, zero padding between
            table       entry count x 64-byte records, sorted by id, no id twice
              0 u64 id   8 u64 offset   16 u64 stored bytes   24 u64 size   32 u64 content hash (Hash64 of the unpacked bytes)
             40 u32 name offset   44 u32 name bytes   48 u32 first dependency   52 u32 dependency count
             56 u8 method (0 stored, 1 DEFLATE, 2 removed)   57 u8 kind (AssetType)   58 u16 flags (0)   60 u32 reserved (0)
            names       the names, UTF-8, no terminators (empty when stripped)
            deps        u64 ids, each entry's list sorted
end - 32    trailer     "OKPKEND\0", u64 file size, u64 Hash64(header + table + names + deps), u64 Hash64(data region)
```

Why this shape: fixed-size records need no parsing and can be checked field by field; the regions must follow each other
exactly, so one comparison per region proves nothing overlaps or hangs past the end; the table is sorted by id, so a
lookup is a binary search with no hash table to build; the metadata hash is checked at open (cheap: the tables only), the
content hash per entry at every read, and the data hash by `VerifyAll`. Data comes first and the tables last so the
writer streams entries out and never holds the game in memory. `kind` is the asset database's `AssetType`.

### Ids and names

`IdOf(name) = Hash64(lower-case ASCII of NormalizeName(name), seed "OKAYID0")`, never 0. Names are the paths inside the
data folder (`Assets/rock.png`, `game.okayscene`), with `/`, no `..`, no drive letters or device names
(`okay::pathsafety` Portable rules). Lower-casing makes `Rock.png` and `rock.png` one id: Windows treats them as one file,
so a project that has both is refused at build with both names. A real 64-bit collision between two different names is
refused the same way. The reader checks that every name gives its entry's id.

### Hash64

Our own non-cryptographic 64-bit hash: four lanes take 8-byte words of each 32-byte stripe (`lane = (lane ^ w) * K1;
lane ^= lane >> 29`), the lanes are folded with a final mix each, the length is mixed in, the last 0-31 bytes are taken in
8-byte words, then a final avalanche (xorshift-multiply three times). Constants are the fractional parts of the golden
ratio and of the square roots of 2 (made odd), 3, 5 and 7, times 2^64, worked out with exact decimal arithmetic. Tests pin
four values, check that streaming in any split equals one call, check avalanche (one flipped input bit flips 31 to 33
output bits on average, at least 12 always) and an even spread of 65 536 similar names. It finds damage, not tampering.

### Patches and the stack

A base package plus patches, in the order `packages.manifest` lists them. A patch replaces or adds entries by id, and its
`removed` entries hide the base's. Each patch records the stack hash of exactly the packages it was made for (base: its
metadata hash; each patch: `Hash64(previous, patch metadata hash)`), so a patch for another version is refused instead of
mixing versions. `MakePatch` hashes the new files against the current stack and writes only what changed (bytes, kind or
dependencies), plus removals.

`packages.manifest` (text: `okaypkg-manifest 1`, then `file size hash` per package) is what an updater compares:
`ChangedPackages(have, wanted)` lists only the packages to download. At start the player checks each listed package's size
(a cut download) and opens it (metadata hash).

### Validation of untrusted packages

Before anything is allocated: size at least header + trailer; magic; version (a newer one says "update the game"); header
and record sizes; unknown flags and reserved fields zero; alignment; entry count, name and dependency table sizes under
`Limits`; the patch flag matches the base hash; the regions follow each other exactly (all sums overflow-checked); the
trailer's file size matches. Then the tables are read and their hash checked. Per entry: id not 0 and strictly
increasing; known method; a removal only in a patch and without data; data inside the data region and aligned; size
under `Limits::maxEntryBytes`; stored entries `stored == size`; DEFLATE entries smaller than their size and not claiming
more than about 1032x their stored bytes (DEFLATE's limit), so a tiny entry cannot ask for gigabytes; name inside the
table, valid, and giving the id; dependencies inside the table. Reads unpack into at most the stated size, require the
whole stream used and the exact size, and check the content hash. Every refusal says why in plain words.

### Engine, editor and player

* **Export** (`editor/BuildGame.cpp`): after the data folder is complete, `PackFolder` packs everything except
  `StreamingAssets/` (opened by name), `build.json` (`--build-info`) and `manifest.json` (anti-cheat list, computed after
  packing, so it hashes `game.okaypkg`). Dependencies come from the asset database's scanners. The package is read back and
  fully verified before the loose files are deleted; a bad cached compressed copy makes it write again without the
  cache. Compressed copies live in `<project>/Library/PackageCache` by content hash, so a second export compresses only
  what changed. Failure keeps loose files and warns. The flat layout (no data folder) and dry runs stay loose.
* **Player** (`player/main.cpp`): before the config is read, `MountFolder` mounts and checks the packages (a damaged or
  incomplete game shows a message and stops), `Materialize` lays the files out once in the per-user cache
  (`<cache>/packages/<game>-<install>/`; a later start with the same packages writes nothing; a file changed by hand is
  written again), and the AssetManager is mounted on the same packages. A game with loose files runs as before.
* **AssetManager**: `MountPackages(stack, root)`; `ReadBytes(path)` reads a packaged file straight from the package
  (hash-checked) and anything else from the disk; `Get/LoadAsync/Stream<AssetBytes>`; `StreamWithDependencies`.

Why the per-user view: most loaders (images, audio, meshes, scenes, fonts) open files by path; moving each one onto
bytes is round 2. Until then the view keeps every part of the engine working unchanged. Cost: the data on disk twice,
and the first start writes it.

### Anti-cheat

What anti-cheat (`shared/AntiCheatIntegrity.hpp`, `okayac::VerifyInstall`) checks, before and after packages:

* **Before (loose files):** at start, every file listed in `Data/manifest.json` is checked (size and SHA-256), and any
  program file (`.exe`, `.dll`, ...) the build did not ship is a problem. The runtime guard (`AntiCheatChecks.hpp`:
  debugger, loaded modules, clock) never hashes files, so a file changed after start was never seen, then or now.
* **After (packed game):** at start, the same manifest check covers `game.okaypkg` (and the loose `StreamingAssets`,
  `build.json`, config files); then `assetpkg::VerifyView` compares **every file of the per-user view byte for byte**
  with its entry read from the package (the package's own Hash64 is not trusted against a deliberate forgery, which is
  why bytes are compared), and lists program files in the view that no package has. Each finding is added with
  `okayac::AddProblems` and counts exactly like a changed shipped file: report mode logs it (and sends it with the run's
  report), enforce mode and players with trusted keys refuse to start. A changed or missing copy is put back from the
  package in every mode. Test: `asset_package`, suites "a data folder, mounted and materialised" (a copy edited with its
  size and time kept, a longer copy, a deleted copy, a planted `.dll`) and "a changed cache copy counts for anti-cheat".
* Cost: with anti-cheat on, every start reads the whole view and unpacks every entry once (before: SHA-256 of every
  loose file). With anti-cheat off nothing is read, as before.

## 4. Benchmarks

`test_asset_package_bench [files] [MB]` on the Linux build container (Release, GCC 13; numbers depend on the machine and
are not checked by the test). Input: 2000 files, 64 MB, two thirds text-like, one third random bytes.

Run on 2026-11-27, 4 cores shared with other builds (load average 7 to 8, so the worker numbers show no gain here; they
were not measured on an idle machine):

| What | Result |
|---|---|
| Hash64 | 3126 MB/s |
| Pack, 1 thread | 2.27 s (28 MB/s in); 64.0 MB -> 25.8 MB (40%), 1334 of 2000 entries compressed |
| Pack, 3 workers | 2.30 s (no gain on the loaded machine) |
| Pack with the cache, nothing changed | 0.14 s (2000 of 2000 entries from the cache), against 2.98 s the first time |
| Pack, 1 file changed | 0.10 s (1999 of 2000 from the cache) |
| Open | 0.94 ms; tables in memory 125 KB for 2000 entries |
| Read every entry (unpack + hash check) | 0.134 s, 476 MB/s |
| Read every loose file (no checks, warm OS cache) | 0.207 s, 309 MB/s |
| Read every entry async on 3 workers | 0.078 s |
| Lookup by id | 250 ns |
| Verify every byte | 0.083 s |
| Player view: first start / next start | 0.22 s (2000 files written) / 0.026 s (2000 kept) |
| Peak memory of the bench program | 70 MB |

Reading: compression is the slow part (`ImageWrite::Deflate`, about 28 MB/s); the cache makes a second export of an
unchanged game about 20 times faster. Reading from the package is faster than opening 2000 loose files, even with the
hash check. Not measured: a cold disk, Windows, a game of gigabytes.

## 5. Tests

`tests/test_asset_package.cpp` (ctest `asset_package`): Hash64 pinned values, streaming, avalanche and spread; names and
ids; round trip from memory and file (stored, compressed, empty, stripped names, 4096 alignment); byte-identical output
whatever the order of adding and with or without workers; collisions with both names; every truncation, every flipped
byte, 32 crafted fields with a resealed trailer, a DEFLATE "bomb"; random mutation fuzz of three seed packages; patches
(changed, added, removed, chained, refused on the wrong base); closure with a cycle; the manifest (strict parse,
changed packages); data folder mount, materialised view (second start writes nothing, hand edits put back, patch
applied, files removed), manifest/size mismatch refused; async reads with 0 and 3 workers; `PackFolder` on an exported
folder (dependencies recorded, loose files removed, kept ones kept, view identical to the originals, cache reuse
byte-identical, a damaged cache caught); the AssetManager mount. The fuzz part also builds alone with
`-fsanitize=address,undefined` (`-DOKAY_ASSETPKG_STANDALONE`; see the top of the test file).

Fuzz results (2026-11-26/27): the standalone build with `-fsanitize=address,undefined -fno-sanitize-recover=undefined`
ran 1 500 000 mutated packages (`OKAY_FUZZ_ROUNDS=1500000`, three seed packages): 291 946 opened, 4 925 read fully
(mutations that leave a valid package, such as a changed `kind` byte with the trailer resealed), no ASan or UBSan report,
no crash, every refusal with a reason. After the last reader changes (2026-11-27) a second run of 500 000 rounds: 97 530 opened,
1 657 read fully, no report. The three-seed sweep runs 30 000 rounds in every ctest run. Tests break when a check
is removed on purpose (checked: without the alignment and metadata-hash checks, 2 checks fail).

## 6. Left for later rounds

1. Loaders reading straight from the package (`Image`, `AudioClip`, meshes, scenes, fonts), then drop the per-user view.
2. Streaming one large entry by chunk (entries are read whole today).
3. Editor: a package window (entries, names, sizes, compression, dependencies), patch making from Build Settings
   (Claude 3's files: needs an `Agree:`), and a Build Settings switch instead of `OKAY_BUILD_LOOSE_FILES`.
4. The update downloader on `packages.manifest`, through content delivery's signed transport.
5. Web and Android exports packing too (the server export goes through Build Game and is packed already).
6. Identical files stored once (dedup by content hash).
