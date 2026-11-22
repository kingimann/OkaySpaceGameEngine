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
| JSON escape and write | planned `Core/Json.hpp` (dedup in progress). For now: `JsonEscape` in `engine/src/Scripting/OkayScriptVM.cpp` | `JsonEscape(s, out)` | `JsonEscape(name, out);  // appends "name" with quotes and escapes` |
| Base64 | planned `Core/Base64.hpp` (dedup in progress). For now: `Base64Decode` in `Render/Gltf.hpp` | `Base64Decode` | `std::vector<std::uint8_t> bytes; Base64Decode(text, bytes);` |
| FNV-1a string hash | `Core/StringId.hpp` | `okay::StringId::HashOf`, `StringId::Make` | `constexpr auto h = StringId::HashOf("player");` |
| SHA-256, HMAC, PBKDF2 | `Crypto/Sha256.hpp` | `okay::crypto::Sha256Hash`, `Sha256`, `HmacSha256Mac`, `Pbkdf2HmacSha256` | `std::uint8_t d[32]; crypto::Sha256Hash(d, data, n);` |
| SHA-512 | `Crypto/Crypto.hpp` | `okay::crypto::Sha512Hash`, `Sha512` | `std::uint8_t d[64]; crypto::Sha512Hash(d, data, n);` |
| CRC-32, Adler-32 | `Core/ImageWrite.hpp` | `okay::ImageWrite::Crc32`, `ImageWrite::Adler32` | `auto c = ImageWrite::Crc32(p, n);  // pass c back in to continue` |
| Inflate (zlib, gzip, raw DEFLATE) | `Core/Inflate.hpp` | `okay::inflate::Zlib`, `inflate::Gzip`, `inflate::Raw` | `inflate::Zlib(in, n, out, {}, why);` |
| Deflate (compress) | `Core/ImageWrite.hpp` | `ImageWrite::Deflate`, `ImageWrite::ZlibCompress` | `auto z = ImageWrite::ZlibCompress(p, n);` |
| Zip writing and listing | `Core/BuildPipeline.hpp` | `okay::buildpipe::WriteZip`, `buildpipe::ReadZipDirectory` | `buildpipe::WriteZip("out.zip", folder, true);` |
| Zip reading (inside the engine) | `engine/src/Render/ZipLite.hpp` | `Archive::Open`, `Archive::Extract` | `Archive a; a.Open(bytes); a.Extract(0, text);` |
| PNG writing | `Core/ImageWrite.hpp` | `ImageWrite::WritePNG`, `ImageWrite::EncodePNG` | `ImageWrite::WritePNG("shot.png", w, h, 4, rgba);` |
| BMP writing | `Platform/BmpWriter.hpp` | `okay::platform::WriteBmp32`, `WriteBmp32Bgra` | `platform::WriteBmp32("shot.bmp", rgba, w, h);` |
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
| Swing and twist split, joint limits | `Math/JointLimit.hpp` | `okay::TwistAbout` | `float deg; Quat twist = TwistAbout(q, axis, deg);` |
| Convex contact (GJK, EPA) | `Physics/ConvexContact.hpp` | `okay::ConvexContact`, `convex_detail::Epa` | `ConvexHit hit = ConvexContact(supportA, supportB);` |
| Word wrap (Game UI and OkayUI) | `UI/TextWrap.hpp` | `okay::textwrap::Greedy`, `textwrap::Options` (`collapseSpaces`, `tabsSeparate`) | `textwrap::Greedy(text, maxW, [&](const std::string& s){ return Measure(s); }, lines);` |
| Tooltip placement (Game UI and OkayUI) | `UI/TooltipPlace.hpp` | `okay::PlaceTooltip`, `TooltipPlaceStyle` | `TooltipBox b = PlaceTooltip(mx, my, w, h, 0, 0, screenW, screenH);` |
| HSV, HSL and hex colours (Game UI, OkayUI, tools) | `Math/ColorSpace.hpp` | `okay::ColorSpace` (`ToHsv`, `FromHsv`, `ToHex`, `ParseHex`) | `Color c = ColorSpace::FromHsv(120.0f, 1.0f, 1.0f);` |
| Ray against a triangle mesh | `Physics/TriMesh.hpp` | `okay::TriMesh::Raycast`, `TriMesh::RayLocal` | `mesh.Raycast(origin, dir, 100.0f, t, normal);` |
| Save games | `Core/GameSaves.hpp` and `Core/SaveFramework.hpp` | `okay::GameSaves` (`Save`, `Load`, `AutoSave`), `Saveable`, `SaveWriter`, `SaveReader`, `SaveSystem` | `GameSaves::Save(scene, "slot1");` |
| Input actions | `Input/ActionSet.hpp` | `okay::ActionSet` (`Pressed`, `Axis`), `ActionMap`, `BindingSpec` | `if (actions.Pressed("Jump")) ...` |
| Saved fields of a component | `Scene/PropertyRegistry.hpp` | `okay::PropertyRegistry` (`Register`, `FindByName`, `TryGetProperty`) | `PropertyRegistry::TryGetProperty(go, "Health.value", v);` |
| Curves and splines | `Math/Curves.hpp` | `okay::Curves` (`CubicBezier`, `CatmullRom`, `ArcLength`) | `Vec3 p = Curves::CatmullRom(p0, p1, p2, p3, t);` |
| Import settings | `Core/ImportSettings.hpp` | `okay::ImportSettings` (`Defaults`, `FromPairs`), `AssetTypeFromPath` | `auto s = ImportSettings::Defaults(AssetTypeFromPath(path));` |
| Logging | `Core/Log.hpp` | `okay::Log` (`Info`, `Warning`, `Error`), `Log::Category` | `Log::Warning("missing texture: ", path);` |
| Script-side JSON, hashing, Base64 (Python, JS, shell) | the standard library | `json.dumps`, `hashlib`, `base64`, `zlib.crc32`; `JSON.stringify`; `jq` | `json.dumps(obj)` |

## Keeping this list true

- Add a row in the same PR that adds a shared helper. Change the row in the same PR that moves one.
- When a dedup lands, move the row from "planned" to the real file, and shrink the baseline:
  `python3 scripts/dup_lint.py scan --update-baseline`, then commit `scripts/dup_baseline.json`.
- `dup_lint.py check` fails when a row (not marked planned) names a file that does not exist.
- When a dedup replaces an old function, mark the old one `OKAY_DEPRECATED("use X")` (`Core/Deprecated.hpp`);
  `dup_lint.py check` then fails any branch that adds a new use of it.
