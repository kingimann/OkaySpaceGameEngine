#!/usr/bin/env bash
# Export tests/export/vs_export.okayscene with the editor's real Build (the same
# one the Build window runs), run the exported game headless, and check that its
# visual scripts printed exactly expected.txt (including the height of a mesh
# built from a bundled .okaymesh asset). Windows CI runs verify.ps1, the same
# check for the Windows build.
#
#   tests/export/verify.sh path/to/OkayEngine [work-dir]
# The player runtime must sit beside the editor (Build copies it from there).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
editor="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
work="${2:-$(mktemp -d)}"
mkdir -p "$work"
out="$work/game"

# The scene points at the .okayvs files by relative path: export from here so
# Build reads the current graphs and embeds them in the exported scene.
(cd "$here" && "$editor" --build-game vs_export.okayscene "$out" --name VsExport)

# Run from elsewhere, so the game can only use what the export embedded.
(cd "$work" && "$out/VsExport" --server --run-for 20 --status 0 --port 45190 --log "$work/run.log") || {
    echo "the exported game exited with an error"; cat "$work/run.log"; exit 1; }

sed -n 's/^.*\(EXPORT .*\)$/\1/p' "$work/run.log" | tr -d '\r' > "$work/actual.txt"
if ! diff -u "$here/expected.txt" "$work/actual.txt"; then
    echo "the exported game's visual scripts printed something else; full log:"; cat "$work/run.log"; exit 1
fi
echo "export check passed ($(wc -l < "$work/actual.txt") lines)"
