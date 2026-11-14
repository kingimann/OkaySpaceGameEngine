#!/usr/bin/env bash
# End-to-end check of the build pipeline's output layout (docs/build_pipeline.md), with the editor's real Build:
# export tests/export's scene as a small project that also holds an unused asset and a StreamingAssets file, then
#   * Folder layout: <Name>.exe next to <Name>_Data/ with the scene, config, build.json, the one asset the scene
#     uses and StreamingAssets/; the unused asset, the visual-script sources and any Data/ folder are NOT there;
#     the player reads it (--build-info) and runs it headless and prints exactly expected.txt
#   * the same game with the data moved beside the exe (the oldest flat layout) still runs
#   * Compact layout (exe + Data/) builds and runs; --no-strip ships the unused asset
#   * Zip layout leaves only a valid .zip; --dry-run writes nothing and reports what it would leave out
#
#   tests/export/verify_layout.sh path/to/OkayEngine [work-dir]
# The player runtime must sit beside the editor (Build copies it from there).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
editor="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
work="${2:-$(mktemp -d)}"; mkdir -p "$work"; work="$(cd "$work" && pwd)"
rm -rf "$work"/*; mkdir -p "$work"
fail() { echo "FAIL: $*"; exit 1; }
need() { [ -e "$1" ] || fail "missing $1"; }
absent() { [ ! -e "$1" ] || fail "should not exist: $1"; }

# A project: Assets/ (the scene, its scripts, the mesh it uses, one asset nothing uses) and StreamingAssets/.
proj="$work/proj"
mkdir -p "$proj/Assets" "$proj/StreamingAssets/levels"
cp "$here"/vs_export.okayscene "$here"/*.okayvs "$here"/Tower.okaymesh "$proj/Assets/"
cp "$here/Tower.okaymesh" "$proj/Assets/Unused.okaymesh"
echo "hello from StreamingAssets" > "$proj/StreamingAssets/hello.txt"
echo '{"level": 1}' > "$proj/StreamingAssets/levels/one.json"
scene="$proj/Assets/vs_export.okayscene"

build() { (cd "$here" && "$editor" --build-game "$scene" "$@"); }
run_game() {   # run_game <exe> <log> <port>
    (cd "$work" && "$1" --server --no-encryption --run-for 20 --status 0 --port "$3" --log "$2") || { echo "the game exited with an error"; cat "$2"; exit 1; }
    sed -n 's/^.*\(EXPORT .*\)$/\1/p' "$2" | tr -d '\r' > "$2.actual"
    diff -u "$here/expected.txt" "$2.actual" || { echo "the game printed something else"; cat "$2"; exit 1; }
}

# ---- Folder layout ----
out="$work/folder"
build "$out" --name "Vs Export" --version 3.1.4 --build-number 7 > "$work/build_folder.txt"
cat "$work/build_folder.txt"
exe="$out/Vs Export"; data="$out/Vs Export_Data"
need "$exe"; need "$data/game.okayscene"; need "$data/game.okayconfig"; need "$data/build.json"
need "$data/Tower.okaymesh"                                  # the asset the scene uses
need "$data/StreamingAssets/hello.txt"; need "$data/StreamingAssets/levels/one.json"
cmp "$proj/StreamingAssets/hello.txt" "$data/StreamingAssets/hello.txt" || fail "StreamingAssets file was changed"
absent "$data/Unused.okaymesh"                               # stripped: nothing uses it
absent "$out/Data"; absent "$data/manifest.json"; absent "$data/Assets"
[ -z "$(find "$data" -name '*.okayvs' -o -name '*.psd' -o -name '*.bak')" ] || fail "authoring files in the build"
grep -q '"game": "Vs Export"' "$data/build.json" || fail "build.json: game"
grep -q '"version": "3.1.4"' "$data/build.json" || fail "build.json: version"
grep -q '"buildNumber": 7' "$data/build.json" || fail "build.json: build number"
grep -q '"layout": "folder"' "$data/build.json" || fail "build.json: layout"
grep -q '"strippedAssets": true' "$data/build.json" || fail "build.json: stripping"
grep -q '"engineVersion": "' "$data/build.json" || fail "build.json: engine version"
grep -q '"scenes": \["game.okayscene"\]' "$data/build.json" || fail "build.json: scenes"
[ -z "$(ls "$out" | grep -v -e '^Vs Export$' -e '^Vs Export_Data$' -e '\.so' -e '\.dll')" ] || fail "unexpected files beside the exe: $(ls "$out")"
(cd "$work" && "$exe" --build-info) | tee "$work/info.txt"
grep -q "Vs Export 3.1.4 (build 7)" "$work/info.txt" || fail "--build-info"
run_game "$exe" "$work/folder.log" 45191
echo "folder layout: ok ($(find "$out" -type f | wc -l) files)"

# ---- an old flat build (everything beside the exe) still runs ----
flat="$work/flat"; mkdir -p "$flat"
cp "$exe" "$flat/Vs Export"; cp -r "$data"/. "$flat/"
for f in "$out"/*.so* "$out"/*.dll; do [ -e "$f" ] && cp "$f" "$flat/"; done
run_game "$flat/Vs Export" "$work/flat.log" 45192
echo "flat (old) layout: ok"

# ---- Compact layout (exe + Data/) ----
build "$work/compact" --name Compact --layout compact > /dev/null
need "$work/compact/Compact"; need "$work/compact/Data/game.okayscene"; need "$work/compact/Data/build.json"
absent "$work/compact/Compact_Data"; absent "$work/compact/Data/Assets/Unused.okaymesh"
grep -q '"layout": "compact"' "$work/compact/Data/build.json" || fail "compact: build.json"
run_game "$work/compact/Compact" "$work/compact.log" 45193
echo "compact layout: ok"

# ---- stripping off ships every project asset ----
build "$work/nostrip" --name NoStrip --no-strip > /dev/null
need "$work/nostrip/NoStrip_Data/Assets/Unused.okaymesh"          # every project asset ships, beside the ones the scene names
need "$work/nostrip/NoStrip_Data/Tower.okaymesh"
grep -q '"strippedAssets": false' "$work/nostrip/NoStrip_Data/build.json" || fail "nostrip: build.json"
echo "no-strip: ok"

# ---- Zip layout keeps only the archive ----
build "$work/zip" --name ZipGame --layout zip --version 2.0.0 --build-number 3 --archive-name "{name}-{version}-b{build}" > "$work/build_zip.txt"
cat "$work/build_zip.txt"
need "$work/zip/ZipGame-2.0.0-b3.zip"
[ "$(ls -A "$work/zip" | wc -l)" = "1" ] || fail "zip layout left more than the archive: $(ls -A "$work/zip")"
if command -v unzip > /dev/null; then
    unzip -tq "$work/zip/ZipGame-2.0.0-b3.zip" > /dev/null || fail "zip is damaged"
    unzip -l "$work/zip/ZipGame-2.0.0-b3.zip" | grep -q "ZipGame_Data/build.json" || fail "zip: build.json"
    unzip -l "$work/zip/ZipGame-2.0.0-b3.zip" | grep -q "ZipGame_Data/Tower.okaymesh" || fail "zip: asset"
    unzip -l "$work/zip/ZipGame-2.0.0-b3.zip" | grep -q "Unused" && fail "zip: unused asset"
fi
grep -q "Archive ZipGame-2.0.0-b3.zip" "$work/build_zip.txt" || fail "zip: size result"
echo "zip layout: ok"

# ---- dry run: the report only ----
build "$work/dry" --name DryGame --dry-run --report "$work/dry_report.txt" > "$work/dry.txt"
cat "$work/dry.txt"
absent "$work/dry"
grep -q "Dry run" "$work/dry.txt" || fail "dry run: summary"
grep -q "Assets/Unused.okaymesh" "$work/dry_report.txt" || fail "dry run: report does not list the unused asset"
grep -q "not used by any scene in the build" "$work/dry_report.txt" || fail "dry run: reason"
grep -q "Tower.okaymesh" "$work/dry_report.txt" || fail "dry run: shipped asset missing from the report"
need "$proj/.okay/build_report.txt"
echo "dry run: ok"

echo "build layout check passed"
