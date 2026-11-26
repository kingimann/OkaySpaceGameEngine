# Shared code: one place for each job

This is the list of the one file to use for each common job. Before you write a helper, find its job here and
use that file. If it does not do what you need, extend it there. Never paste its code into another file.

`scripts/dup_lint.py` checks this in CI (see [no_duplicates.md](../process/no_duplicates.md)). It fails a branch
that adds a block pasted from elsewhere, or a new copy of a helper from this table (for example the FNV-1a
constants, the CRC-32 polynomial, the Base64 alphabet, or a hand-made `Trim`). It also checks that every file named
in this table exists, except in rows marked "planned".

Paths are under `engine/include/okay/` unless they say otherwise. "Planned (dedup in progress)" means the shared
file does not exist yet: other agents are merging the copies into it. Until it lands, use the file named as
"for now", and do not add another copy.

## Before you write a helper

1. Search this table, then the [code index](code_index.md) (every public class and function, generated), then the
   code: `python3 scripts/find_existing.py <words>` searches all three and lists names close to yours.
2. Found it: call it. It does nearly what you need: extend the shared one (add a parameter or an overload) in the
   same PR, and update this table.
3. Not found: write it once, in the right shared file (`Core/`, `Math/`, ...), add a row here, and use it from every
   place that needs it.
4. Never paste a block from another file, not even "for now". If a copy is really needed (a file format table that
   must match a spec, a test that must not depend on the code it tests), mark the line with
   `dup-allow: <reason>` and explain it in the PR.
5. When you claim work on issue #422, add an `Existing:` line that names the code you build on.

## The table

| Job | Use this file | Names | Example |
|---|---|---|---|
| JSON read | planned `Core/Json.hpp` (dedup in progress). For now: `JsonReader` in `engine/src/Scripting/OkayScriptVM.cpp` is the reader to copy behaviour from | planned `okay::json::Parse` | planned: `auto v = json::Parse(text);` |
| JSON escape and write | planned `Core/Json.hpp` (dedup in progress). For now: `okayscript::detail::JsonEscape` in `okayscript/include/okayscript/CoreBuiltins.hpp` (the script builtins; the engine includes it) | `JsonEscape(s, out)` | `JsonEscape(name, out);  // appends "name" with quotes and escapes` |
| Online service request (headers, sending, test recording) | `engine/include/okay/Backend/Supabase.hpp` (`MakeRequest`), `engine/include/okay/Backend/Transport.hpp` (OkayBackend) | `okay::backend::supabase::MakeRequest`, `okay::backend::Transport`, `RecordingTransport` | `t.Send(supabase::MakeRequest(key, "GET", url, {}, token));` |
| Table rows and database functions (PostgREST query text: select, filters, order, limit, insert / upsert / update / delete, RPC) | `engine/include/okay/Backend/Database.hpp` (OkayBackend) | `okay::backend::Database`, `Query`, `AccountService::Db()` / `DbAnon()` | `svc.Db().From("cloud_saves").Select("data").Eq("key", k).Get();` |
| Read a claim from a JSON web token (no signature check) | `engine/include/okay/Backend/Auth.hpp` | `okay::backend::TokenClaim` | `TokenClaim(jwt, "sub")` |
| Base64 | planned `Core/Base64.hpp` (dedup in progress). For now: `Base64Decode` in `Render/Gltf.hpp` | `Base64Decode` | `std::vector<std::uint8_t> bytes; Base64Decode(text, bytes);` |
| FNV-1a string hash | `Core/StringId.hpp` | `okay::StringId::HashOf`, `StringId::Make` | `constexpr auto h = StringId::HashOf("player");` |
| SHA-256, HMAC, PBKDF2 | `Crypto/Sha256.hpp` | `okay::crypto::Sha256Hash`, `Sha256`, `HmacSha256Mac`, `Pbkdf2HmacSha256` | `std::uint8_t d[32]; crypto::Sha256Hash(d, data, n);` |
| SHA-512 | `Crypto/Crypto.hpp` | `okay::crypto::Sha512Hash`, `Sha512` | `std::uint8_t d[64]; crypto::Sha512Hash(d, data, n);` |
| CRC-32, Adler-32 | `Core/ImageWrite.hpp` | `okay::ImageWrite::Crc32`, `ImageWrite::Adler32` | `auto c = ImageWrite::Crc32(p, n);  // pass c back in to continue` |
| Inflate (zlib, gzip, raw DEFLATE) | `Core/Inflate.hpp` | `okay::inflate::Zlib`, `inflate::Gzip`, `inflate::Raw` | `inflate::Zlib(in, n, out, {}, why);` |
| Brotli decoder (RFC 7932; library `okay_brotli`) | `Core/Brotli.hpp` | `okay::brotli::Decode` | `brotli::Decode(in, n, out, opts, why);` |
| Deflate (compress) | `Core/ImageWrite.hpp` | `ImageWrite::Deflate`, `ImageWrite::ZlibCompress` | `auto z = ImageWrite::ZlibCompress(p, n);` |
| Zip writing and listing | `Core/BuildPipeline.hpp` | `okay::buildpipe::WriteZip`, `buildpipe::ReadZipDirectory` | `buildpipe::WriteZip("out.zip", folder, true);` |
| Zip reading (inside the engine) | `engine/src/Render/ZipLite.hpp` | `Archive::Open`, `Archive::Extract` | `Archive a; a.Open(bytes); a.Extract(0, text);` |
| PNG writing | `Core/ImageWrite.hpp` | `ImageWrite::WritePNG`, `ImageWrite::EncodePNG` | `ImageWrite::WritePNG("shot.png", w, h, 4, rgba);` |
| BMP writing | `Platform/BmpWriter.hpp` | `okay::platform::WriteBmp32`, `WriteBmp32Bgra` | `platform::WriteBmp32("shot.bmp", rgba, w, h);` |
| Network tests: a script's or graph's variable, an overlay line | `tests/net_test_util.hpp` | `nettest::ScriptVar`, `nettest::GraphVar`, `nettest::HasLine` | `CHECK(nettest::ScriptVar(go, "code") == "K7Q29X");` |
| One slow call off the editor's frame (network, a whole-project read), its result taken in a later frame | `editor/WorkerCall.hpp` (threads through `shared/OkayThreads.hpp`, CLAUDE.md 21a) | `oked::WorkerCall` (`Start`, `Collect`, `Busy`, `Drop`) | `call.Start([out]{ *out = updater::CheckLatest(); }, "okay-update-check"); ... if (call.Collect()) Use(*out);` |
| Random numbers | `Core/Random.hpp` | `okay::Random` (`Seed`, `Value`, `Range`) | `Random r(seed); float x = r.Range(-1.0f, 1.0f);` |
| Noise (Perlin, simplex, value, Worley, fBm) | `Math/Noise.hpp` | `okay::NoiseField` (`Perlin2`, `Perlin3`, `Simplex2`, `Fbm2`) | `NoiseField n(7); float h = n.Fbm2(NoiseType::Perlin, x, z);` |
| Easing curves | `Math/Easing.hpp` | `okay::Ease`, `Easing::Evaluate`, `Easing::QuadIn` ... | `float k = Easing::Evaluate(Ease::CubicOut, t);` |
| Tweens | `Core/Tween.hpp` | `okay::TweenManager` (`To`, `ToVec3`, `Virtual`), reached with `scene.tweens()` | `scene.tweens().Virtual(0, 1, 0.5f, [&](float v){ a = v; });` |
| Timers and delayed calls | `Core/Scheduler.hpp` | `okay::Scheduler` (`Invoke`, `InvokeRepeating`, `Cancel`), reached with `scene.scheduler()` | `scene.scheduler().Invoke(2.0f, [&]{ Explode(); });` |
| Camera shake | `Components/CameraMath.hpp` | `okay::cam::CameraShaker` (`Add`, `SetHandheld`) | `shaker.Add(ShakePreset::Explosion, 1.0f);` |
| Object pools | `Scene/ObjectPool.hpp` | `okay::ObjectPool` (`Get`, `Release`) | `GameObject* b = bullets.Get(muzzle); ... bullets.Release(b);` |
| Health of any object | `Components/Damage.hpp` (body in `engine/src/Components/Damage.cpp`) | `okay::HealthOf`, `DealDamage` | `float h, m; if (HealthOf(go, h, m)) ...` |
| Typed events | `Core/EventBus.hpp` | `okay::EventBus` (`Subscribe`, `Publish`) | `bus.Subscribe<PlayerDied>(fn); bus.Publish(PlayerDied{id});` |
| Messages to ActionLists | planned one broadcast helper next to `Components/ActionList.hpp` (dedup in progress). Do not write another `FindObjectsOfType<ActionList>` + `ReceiveMessage` loop | `ActionList::ReceiveMessage` | planned |
| String helpers (Trim, Split, ToLower) | planned `Core/StringUtil.hpp` (dedup in progress) | planned `okay::str::Trim`, `Split`, `ToLower` | planned |
| Two-bone IK | `Math/TwoBoneIK.hpp` | `okay::SolveTwoBoneIK`, `SolveTwoBoneIKSoft` | `SolveTwoBoneIK(root, upper, lower, target, pole, mid, end);` |
| Swing and twist split, joint limits | `Animation/JointLimit.hpp` | `okay::TwistAbout` | `float deg; Quat twist = TwistAbout(q, axis, deg);` |
| Convex contact (GJK, EPA) | `Physics/ConvexContact.hpp` | `okay::ConvexContact`, `convex_detail::Epa` | `ConvexHit hit = ConvexContact(supportA, supportB);` |
| Engine math to the physics core and back | `Physics/Core/VecConvert.hpp` | `phys::conv::P3`, `E3`, `PQ`, `EQ` (Vec3 / Quat <-> `phys::V3` / `phys::Q`) | `using namespace phys::conv; world.SetLinearVelocity(id, P3(v));` |
| Grid A* (2D grids, tilemaps, the 3D probe grid) | `AI/GridAStar.hpp`, called through `AI/Navigation.hpp` | `okay::gridastar::Search`, `DenseStore`, `SparseStore`, `GridCell`; `Navigation::GridPath`, `Navigation::TilemapPath` | `auto path = Navigation::GridPath(walkable, w, h, 0, 0, 9, 8, true);` |
| Ray against a triangle mesh | `Physics/TriMesh.hpp` | `okay::TriMesh::Raycast`, `TriMesh::RayLocal` | `mesh.Raycast(origin, dir, 100.0f, t, normal);` |
| Quaternion to rotation matrix (any precision) | `Math/Mat4.hpp` | `okay::SetRotationFromUnitQuat`, `Mat4::Rotate` | `rc::M4 r; SetRotationFromUnitQuat(r, x, y, z, w);` |
| Model readers: affine inverse, cofactors, Quake frame vertices, triangle corners | `engine/src/Render/AffineInverse.hpp`, `engine/src/Render/ReaderCommon.hpp` | `rc::InvAffine`, `rc::Cofactors3`, `rc::ReadQuakeFrameVerts`, `rc::TriCorner` | `double c[9]; rc::Cofactors3(m, c);` |
| RHI backends: resource maps (size, back buffer, count, erase, pipeline + shader lookup) | `engine/src/Render/RHI/BackendTables.hpp` | `table::Count`, `table::TextureSize`, `table::BackBuffer`, `table::Erase`, `table::FindPipeline` | `return table::Count(m_buffers, m_textures);` |
| Tests: memory and time budget of a read (counting operator new) | `tests/alloc_budget.hpp` (one source file per test program) | `budget::Measure`, `budget::PeakReset`, `budget::PeakSince`, `g_live`, `g_peak` | `Run r = budget::Measure("big", read, kCap, kMaxSeconds);` |
| Tests: hostile-input helpers for readers | `tests/reader_hostile_common.hpp` | `hostile::Sane`, `hostile::MutateBinary`, `hostile::HasNote`, `hostile::AddCorpusSeeds` | `hostile::AddCorpusSeeds(seeds, {"TER/a.ter"});` |
| Tests: own-reader routing through the import seam | `tests/own_seam_checks.hpp` | `seam::CheckSeamMatchesDirect`, `seam::CheckFlattened`, `seam::CheckEnvSwitch`, `seam::CheckDefaultBackend`, `seam::SetEnv` | `seam::CheckFlattened(path, 2);` |
| Save games | `Core/GameSaves.hpp` and `Core/SaveFramework.hpp` | `okay::GameSaves` (`Save`, `Load`, `AutoSave`), `Saveable`, `SaveWriter`, `SaveReader`, `SaveSystem` | `GameSaves::Save(scene, "slot1");` |
| Input actions (the one input system) | `Input/ActionSet.hpp`, `Input/InputRuntime.hpp`, `Input/InputActions.hpp` | `okay::InputRuntime::Actions()` (the one table), `okay::ActionSet` (`Pressed`, `Axis`), `ActionMap`, `BindingSpec`; `InputActions` is its name-based front | `if (InputActions::Down("Jump")) ...` or `if (InputRuntime::Actions().Pressed("Jump")) ...` |
| Saved fields of a component | `Scene/PropertyRegistry.hpp` | `okay::PropertyRegistry` (`Register`, `FindByName`, `TryGetProperty`) | `PropertyRegistry::TryGetProperty(go, "Health.value", v);` |
| Curves and splines | `Math/Curves.hpp` | `okay::Curves` (`CubicBezier`, `CatmullRom`, `ArcLength`) | `Vec3 p = Curves::CatmullRom(p0, p1, p2, p3, t);` |
| Import settings | `Core/ImportSettings.hpp` | `okay::ImportSettings` (`Defaults`, `FromPairs`), `AssetTypeFromPath` | `auto s = ImportSettings::Defaults(AssetTypeFromPath(path));` |
| An engine function for scripts (OkayScript, C#, visual-script nodes) | `Scripting/ScriptApiRegistry.hpp`; the engine's entries are in `engine/src/Scripting/ScriptApiCore.cpp` ([guide](../scripting/script_api_registry.md)). Do not add a new one to `OkayScriptVM.cpp` or `EngineApi.cpp` | `okay::ScriptApiEntry`, `AddScriptApi`, `ScriptApis`, `ScriptApiError` | `add(Fn("time", "Time", "time", {}, T::Float, fn, "Seconds since the game started."));` |
| Tests: SQL checks on a local Postgres (skip unless `OKAY_SQL_TEST_PGHOST`, fresh database, load files, count PASS/FAIL) | `tests/scripts/sql_test_lib.sh`, `tests/sql/test_helpers.sql` (paths from the repository root; the older `test_*_sql.sh` scripts still carry their own copies) | `sqltest_begin`, `sqltest_load`, `sqltest_checks`; `t.ok`, `t.as_user`, `t.try`, `t.val` | `sqltest_begin my_sql okay_my_sqltest; sqltest_load -f "$REPO/docs/sql/x.sql"` |
| Logging | `Core/Log.hpp` | `okay::Log` (`Info`, `Warning`, `Error`), `Log::Category` | `Log::Warning("missing texture: ", path);` |
| Script-side JSON, hashing, Base64 (Python, JS, shell) | the standard library | `json.dumps`, `hashlib`, `base64`, `zlib.crc32`; `JSON.stringify`; `jq` | `json.dumps(obj)` |
| okay-ms language tests: build a program with okay-cl at /O0 /O1 /O2, run it under Wine, compare with MinGW g++ | `tools/okayms/tests/program_check.hpp` (paths in this row are from the repository root) | `okayms_test::run_both`, `ProgramRun`, `wine_exit`, `bump_alloc` | `okayms_test::run_both(kRun, tally, "name", source, 0);` |
| Fuzzy match of typed letters (Quick Open, palettes, autocomplete) | `editor/ScriptIde.hpp` (paths in this row are from the repository root) | `scriptide::FuzzyScore` (`oked::FuzzyScore` in `editor/ScriptAssist.cpp` calls it); the visual-script search keeps its own scorer `VsFuzzyScore` (`editor/VisualScriptNodeDocs.hpp`, word-aware, different weights) | `if (scriptide::FuzzyScore("plc", "PlayerController.okay") >= 0) ...` |
| Script text: find / replace with case, whole word, regex; text edits; line diff | `editor/ScriptIde.hpp` (from the repository root) | `scriptide::FindAll`, `ReplaceAll`, `FindInTexts`, `ApplyEdits`, `TextEdit`, `LineDiff` | `int n; auto t = scriptide::ReplaceAll(src, "(\\w+) = 1", "$1 = 2", {true}, &n);` |
| Commands and providers editor plugins add to a window | `editor/PluginRegistry.hpp` (from the repository root) | `pluginreg::PluginRegistry`, `Command`, `RankCommands`, `ParseShortcut`, `ScriptEditorPlugins()` | `reg.AddCommand({"id", "Title", "Category", "Ctrl+Alt+T", owner, run});` |

## Keeping this list true

- Add a row in the same PR that adds a shared helper. Change the row in the same PR that moves one.
- When a dedup lands, move the row from "planned" to the real file, and shrink the baseline:
  `python3 scripts/dup_lint.py scan --update-baseline`, then commit `scripts/dup_baseline.json`.
- `dup_lint.py check` fails when a row (not marked planned) names a file that does not exist.
- When a dedup replaces an old function, mark the old one `OKAY_DEPRECATED("use X")` (`Core/Deprecated.hpp`);
  `dup_lint.py check` then fails any branch that adds a new use of it.
