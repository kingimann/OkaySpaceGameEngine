#!/usr/bin/env bash
# Tests the Windows installer (scripts/okayspace-installer.nsi): Setup and the uninstaller, built with makensis and run
# under wine on Linux, in silent mode (/S). It turns the audit's wine recipe (docs/install_audit.md, Appendix A) into
# checks. Nothing here needs Windows, and nothing here proves what the dialogs look like (see docs/installer.md for what
# was and was not seen).
#
# Needs: makensis and python3 (the build and file-list checks), and for the wine scenarios also wine (64-bit is enough), a MinGW compiler
# (for tiny test programs), iconv and strings. Exits 77 (ctest: skipped) when makensis or python3 is missing, or, after the build and
# file-list checks passed, when the wine scenarios cannot run. A failed check is always exit 1.
#
# This wine cannot run 32-bit programs, so the installer is built as a 64-bit installer for the test with
# makensis "-XTarget amd64-unicode" (a command-line switch; the script itself is not changed and is not copied).
#
# Environment:
#   OKAY_INSTALLER_NSI     the script to test (default scripts/okayspace-installer.nsi). Point it at an older copy to see
#                          the checks fail against it.
#   OKAY_INSTALLER_ONLY    only run these scenarios, for example "fresh update" (names are the sc_* functions below).
#   OKAY_INSTALLER_VERBOSE 1 = print each run's exit code and the installer log (also printed once, after a failed check).
#   OKAY_INSTALLER_PREFIX  an existing wine prefix to reuse (it is not deleted); default is a new one (about 700 MB,
#                          deleted at the end). Put TMPDIR on a tmpfs such as /dev/shm to keep it off the disk.
#   OKAY_WINE              the wine launcher to use.
#   TMPDIR                 where the scratch folder goes.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
NSI="${OKAY_INSTALLER_NSI:-$REPO/scripts/okayspace-installer.nsi}"
GEN="$REPO/scripts/gen-installer-filelist.py"

skip() { echo "skipped: $1"; exit 77; }

for t in makensis python3 sha256sum; do
  command -v "$t" >/dev/null 2>&1 || skip "$t not found"
done
[ -f "$NSI" ] || { echo "installer script not found: $NSI"; exit 1; }

# The wine scenarios need more; without it only the build and file-list checks run and the exit code is 77 at the end.
WINE_OK=1; WHY_NOT=""
no_wine() { WINE_OK=0; [ -z "$WHY_NOT" ] && WHY_NOT="$1"; }
for t in iconv x86_64-w64-mingw32-gcc strings; do command -v "$t" >/dev/null 2>&1 || no_wine "$t not found"; done
[ "${OKAY_INSTALLER_NO_WINE:-0}" = 1 ] && no_wine "OKAY_INSTALLER_NO_WINE=1"
WINE="${OKAY_WINE:-}"
if [ -z "$WINE" ]; then
  for c in wine64 wine /usr/lib/wine/wine64 /usr/lib/x86_64-linux-gnu/wine/wine64; do
    if command -v "$c" >/dev/null 2>&1; then WINE="$(command -v "$c")"; break; fi
  done
fi
WINESERVER=""
if [ -n "$WINE" ]; then
  WINESERVER="$(dirname "$(readlink -f "$WINE")")/wineserver"
  [ -x "$WINESERVER" ] || WINESERVER="$(command -v wineserver 2>/dev/null || true)"
fi
[ -n "$WINE" ] && [ -n "$WINESERVER" ] || no_wine "wine not found"

T="$(mktemp -d "${TMPDIR:-/tmp}/okay-installer-test.XXXXXX")" || skip "no scratch folder"
OWN_PREFIX=1
PFX="$T/prefix"
if [ -n "${OKAY_INSTALLER_PREFIX:-}" ]; then PFX="$OKAY_INSTALLER_PREFIX"; OWN_PREFIX=0; fi
MOUNTS=""

cleanup() {
  W_server_kill
  local m
  for m in $MOUNTS; do umount "$m" 2>/dev/null || umount -l "$m" 2>/dev/null; done
  # Never remove a folder that is still a mount point.
  for m in $MOUNTS; do mountpoint -q "$m" 2>/dev/null && { echo "warning: $m is still mounted, not removing $T"; return; }; done
  [ "$OWN_PREFIX" = 1 ] && rm -rf "$PFX"
  rm -rf "$T"
}
trap cleanup EXIT

export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" LC_ALL=C.UTF-8
export WINEPREFIX="$PFX"
unset DISPLAY
# Wine 9 crashes ("free(): invalid pointer") when TMPDIR points at some folders such as /dev/shm; the scratch folder is already made.
unset TMPDIR
# W runs a Windows program; the exit code is the program's.
W() { "$WINE" "$@"; }
W_server_kill() { WINEPREFIX="$PFX" "$WINESERVER" -k >/dev/null 2>&1; }
# Windows path of a Linux path (the Z: drive is the Linux root).
wp() { local p="$1"; printf 'Z:%s' "${p//\//\\}"; }
# Linux path of a Windows path on the C: drive.
up() { local p="${1#C:\\}"; printf '%s/drive_c/%s' "$PFX" "${p//\\//}"; }

# ---- checks ---------------------------------------------------------------------------------------------------------
fail=0; nchk=0; nfail=0
SHOWN=""
check() { # check <status> <message> [detail]
  nchk=$((nchk+1))
  if [ "$1" = 0 ]; then echo "  ok   $2"; else
    nfail=$((nfail+1)); fail=1; echo "  FAIL $2"; [ -n "${3:-}" ] && echo "       $3"
    if [ "$SHOWN" != "$LASTRUN" ] && [ -n "$LASTRUN" ] && [ "${OKAY_INSTALLER_VERBOSE:-0}" != 1 ]; then
      SHOWN="$LASTRUN"; echo "       (last run: $LASTRUN)"; { slog; ulog; } | tail -8 | sed 's/^/       | /'
    fi
  fi
}
expect_rc() { [ "$RC" = "$1" ]; check $? "$2 (exit code $1)" "exit code was $RC"; }
expect_nonzero() { [ "$RC" != 0 ]; check $? "$1 (non-zero exit code)" "exit code was $RC"; }
has_file() { [ -f "$1" ]; check $? "$2" "missing: $1"; }
no_file() { [ ! -e "$1" ]; check $? "$2" "still there: $1"; }
log_has() { printf '%s\n' "$LOGTEXT" | grep -qF -- "$1"; check $? "$2" "the log does not say: $1"; }
snap() { (cd "$1" 2>/dev/null && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum); }
same_tree() { [ "$1" = "$2" ]; check $? "$3" "the folder changed"; }

# ---- the scratch machine ----------------------------------------------------------------------------------------------
echo "== setting up (test programs, installers$([ "$WINE_OK" = 1 ] && echo ", wine prefix"))"
LAD_W=""; ARP='HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\OkaySpace'; APPKEY='HKCU\Software\OkaySpace'
if [ "$WINE_OK" = 1 ]; then
  if [ ! -d "$PFX/drive_c" ]; then W wineboot --init >/dev/null 2>&1; "$WINESERVER" -w >/dev/null 2>&1; fi
  LAD_W="$(W cmd /c "echo %LOCALAPPDATA%" 2>/dev/null | tr -d '\r\n')"
  APPD_W="$(W cmd /c "echo %APPDATA%" 2>/dev/null | tr -d '\r\n')"
  TMPW_W="$(W cmd /c "echo %TEMP%" 2>/dev/null | tr -d '\r\n')"
  case "$LAD_W" in C:\\*) ;; *) no_wine "this wine could not run a Windows program (got '$LAD_W')" ;; esac
fi
if [ "$WINE_OK" = 1 ]; then
  LAD="$(up "$LAD_W")"; APPD="$(up "$APPD_W")"; TMPW="$(up "$TMPW_W")"
  SMDIR="$APPD/Microsoft/Windows/Start Menu/Programs"
  DESK="$(dirname "$(dirname "$LAD")")/Desktop"   # C:\users\<name>\Desktop
  INST_W="$LAD_W\\OkaySpace"; INST="$LAD/OkaySpace"
fi

# Tiny Windows programs: one that writes a "ready" file and then sleeps (a program that is running). Without wine the build
# checks only need files with the right names, so a placeholder will do.
cat > "$T/sleeper.c" <<'EOF'
#include <windows.h>
#include <stdio.h>
int main(int argc, char** argv) {
    if (argc > 1) { FILE* f = fopen(argv[1], "w"); if (f) { fputs("ready", f); fclose(f); } }
    Sleep(300000);
    return 0;
}
EOF
if [ "$WINE_OK" = 1 ]; then
  x86_64-w64-mingw32-gcc -O1 -s "$T/sleeper.c" -o "$T/sleeper.exe" 2>"$T/gcc.txt" || no_wine "cannot build the test program: $(head -1 "$T/gcc.txt")"
fi
[ -s "$T/sleeper.exe" ] || printf 'MZ placeholder program for the build checks' > "$T/sleeper.exe"

# A distribution laid out like scripts/build-windows-dist.sh writes it.
#   mkdist <folder> <version> <pad bytes per program> <extras: stale|new|many|none>
mkdist() {
  local d="$1" ver="$2" pad="$3" extra="${4:-none}" p t i
  rm -rf "$d"
  mkdir -p "$d/Tools" "$d/Assets/Textures" "$d/Assets/Fonts" "$d/examples/vehicle" "$d/examples/own_wav" "$d/examples/behavior_trees"
  for p in OkaySpace.exe Tools/OkayEngine.exe Tools/OkaySpacePlayer.exe Tools/okayspace-relay.exe Tools/okayui_d3d11_demo.exe; do
    cp "$T/sleeper.exe" "$d/$p"; printf 'marker %s %s' "$p" "$ver" >> "$d/$p"
    [ "$pad" -gt 0 ] && head -c "$pad" /dev/zero >> "$d/$p"
  done
  printf 'SDL2 dll bytes %s' "$ver" > "$d/SDL2.dll"; cp "$d/SDL2.dll" "$d/Tools/SDL2.dll"
  printf '%s\n' "$ver" > "$d/VERSION.txt"; cp "$d/VERSION.txt" "$d/Tools/VERSION.txt"
  printf 'OkaySpace readme %s\n' "$ver" > "$d/README.txt"
  printf 'accounts %s\n' "$ver" > "$d/Tools/accounts.md"
  for t in dirt grass grid stone; do printf 'png %s %s' "$t" "$ver" > "$d/Assets/Textures/$t.png"; done
  printf 'ttf %s' "$ver" > "$d/Assets/Fonts/DejaVuSans.ttf"
  printf 'license %s' "$ver" > "$d/Assets/Fonts/DejaVuSans-LICENSE.txt"
  printf 'scene %s' "$ver" > "$d/examples/vehicle/vehicle_test.okayscene"
  printf 'RIFF wav %s' "$ver" > "$d/examples/own_wav/mulaw_tone.wav"
  printf 'scene wav %s' "$ver" > "$d/examples/own_wav/own_wav_test.okayscene"
  printf 'bt %s' "$ver" > "$d/examples/behavior_trees/GuardBrain.okaybt"
  case "$extra" in
    stale) printf 'an old tool %s' "$ver" > "$d/Tools/old_tool.exe"; printf 'old sample' > "$d/examples/vehicle/old_sample.okayscene" ;;
    new)   printf 'a new tool %s' "$ver" > "$d/Tools/new_tool.exe" ;;
    many)  mkdir -p "$d/examples/many"; for i in $(seq 1 300); do printf 'x' > "$d/examples/many/f$i.txt"; done ;;
  esac
}
# makensis the way the release job does it, plus the switch that makes this wine able to run it.
#   build_setup <dist> <out.exe> <version> [more makensis args]
build_setup() {
  local dist="$1" out="$2" ver="$3"; shift 3
  makensis -V1 "-XTarget amd64-unicode" -DVERSION="$ver" -DDISTDIR="$dist" -DOUTFILE="$out" "$@" "$NSI" >"$T/makensis.txt" 2>&1
  local rc=$?
  [ -s "$out" ] || { echo "makensis failed ($rc) for $ver:"; sed 's/^/    /' "$T/makensis.txt" | head -20; return 1; }
  return 0
}

mkdist "$T/d1" 9.9.9 0 stale
mkdist "$T/d2" 9.9.10 0 new
mkdist "$T/d0" 9.9.8 0 none
build_setup "$T/d1" "$T/Setup-9.9.9.exe" 9.9.9 || exit 1
build_setup "$T/d2" "$T/Setup-9.9.10.exe" 9.9.10 || exit 1
build_setup "$T/d0" "$T/Setup-9.9.8.exe" 9.9.8 || exit 1
# Bigger programs for the disk-space scenarios (the second one is bigger than the first, so an update cannot fit in place).
mkdist "$T/b1" 9.9.9 600000 none
mkdist "$T/b2" 9.9.10 1000000 many
build_setup "$T/b1" "$T/Setup-big-9.9.9.exe" 9.9.9 || exit 1
build_setup "$T/b2" "$T/Setup-big-9.9.10.exe" 9.9.10 || exit 1
# Same bigger update, but with the free-space check turned down so that it passes and the disk fills during unpacking.
build_setup "$T/b2" "$T/Setup-big-9.9.10-nomargin.exe" 9.9.10 -DSPACE_MARGIN_KB=-3500 || exit 1

# ---- helpers that act on the scratch machine --------------------------------------------------------------------------------
BGPIDS=""
stop_bg() { local p; for p in $BGPIDS; do kill "$p" 2>/dev/null; done; BGPIDS=""; W_server_kill; sleep 0.3; }
# Start a program from the installed folder and wait until it says it is running.
start_program() { # start_program <windows path>
  local ready="$T/ready.$RANDOM"; rm -f "$ready"
  W "$1" "$(wp "$ready")" >/dev/null 2>&1 &
  BGPIDS="$BGPIDS $!"
  local i; for i in $(seq 1 160); do [ -f "$ready" ] && return 0; sleep 0.25; done   # up to 40 s: a loaded machine starts wine slowly
  return 1
}
reset() {
  stop_bg
  rm -rf "$INST" "$APPD/OkaySpace" "$SMDIR/OkaySpace" "$SMDIR/OkaySpace.lnk" "$DESK"/OkaySpace.* "$TMPW"/OkaySpace-*.log \
         "$TMPW"/okayspace_* "$TMPW"/~nsu* "$T/work"
  W reg delete "$ARP" /f >/dev/null 2>&1
  W reg delete "$APPKEY" /f >/dev/null 2>&1
  mkdir -p "$T/work" "$SMDIR" "$TMPW" "$DESK"
}
LASTRUN=""
note_run() { # note_run <what> : remember (and with OKAY_INSTALLER_VERBOSE print) the exit code and the log of a run
  LASTRUN="$1: exit code $RC"
  if [ "${OKAY_INSTALLER_VERBOSE:-0}" = 1 ]; then echo "    [$LASTRUN]"; { slog; ulog; } | sed 's/^/    | /'; fi
}
setup() { # setup <installer> [args...]  (sets RC; the output goes to $T/out.txt)
  rm -f "$TMPW/OkaySpace-setup.log"
  W "$@" >"$T/out.txt" 2>&1; RC=$?
  note_run "Setup ${*:2}"
}
setup_cmd() { # setup_cmd <installer> <switches...>  (through cmd.exe, for a /D= path with spaces; sets RC)
  rm -f "$TMPW/OkaySpace-setup.log"
  W cmd /c "$*" >"$T/out.txt" 2>&1; RC=$?
  note_run "Setup (cmd) $*"
}
uninstall_wait() { # the real flow: the first process copies itself to Temp and returns at once, so wait for wine to go quiet
  rm -f "$TMPW/OkaySpace-uninstall.log"
  W "$INST_W\\Uninstall.exe" "$@" >"$T/out.txt" 2>&1; RC=$?
  "$WINESERVER" -w >/dev/null 2>&1
  note_run "Uninstall $*"
}
uninstall_sync() { # runs in place and waits, so the exit code is the real one (Uninstall.exe cannot delete itself this way)
  rm -f "$TMPW/OkaySpace-uninstall.log"
  W "$INST_W\\Uninstall.exe" "$@" "_?=$INST_W" >"$T/out.txt" 2>&1; RC=$?
  note_run "Uninstall (in place) $*"
}
slog() { iconv -f UTF-16 -t UTF-8 "$TMPW/OkaySpace-setup.log" 2>/dev/null | tr -d '\r'; }
ulog() { iconv -f UTF-16 -t UTF-8 "$TMPW/OkaySpace-uninstall.log" 2>/dev/null | tr -d '\r'; }
# (wine's reg.exe prints "Unable to find the specified registry key" on stdout, so only a real key listing counts.)
regdump() { local o; o="$(W reg query "$1" 2>/dev/null | tr -d '\r')"; case "$o" in HKEY_*|*$'\n'HKEY_*) printf '%s\n' "$o" ;; esac; }
regget() { printf '%s\n' "$1" | sed -n "s/^    $2    REG_[A-Z_]*    //p" | head -1; }
regdword() { printf '%s\n' "$1" | sed -n "s/^    $2    REG_DWORD    //p" | head -1; }
# All the files of a distribution folder are in the install folder with the same bytes.
dist_installed() { # dist_installed <dist> <install folder> <message>
  local a b
  a="$(cd "$1" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum)"
  b="$(cd "$1" && find . -type f -print0 | LC_ALL=C sort -z | (cd "$2" 2>/dev/null && xargs -0 -r sha256sum 2>&1))"
  [ "$a" = "$b" ]; check $? "$3" "the installed files differ from the distribution: $(diff <(printf '%s\n' "$a") <(printf '%s\n' "$b") | head -4 | tr '\n' ';')"
}
nothing_installed() { # nothing_installed <folder> <message prefix>
  no_file "$1/OkaySpace.exe" "$2: no launcher in the folder"
  no_file "$1/Uninstall.exe" "$2: no uninstaller"
  [ -z "$(regdump "$ARP")" ]; check $? "$2: no Apps & features entry" "the entry exists"
  [ -z "$(regdump "$APPKEY")" ]; check $? "$2: no install folder remembered in the registry" "the key exists"
  [ ! -e "$SMDIR/OkaySpace" ] && [ ! -e "$SMDIR/OkaySpace.lnk" ] && [ ! -e "$DESK/OkaySpace.lnk" ]; check $? "$2: no shortcuts" "a shortcut exists"
}
no_leftovers() { # no stage folder and no .old files in an install folder
  local n; n="$(find "$1" \( -name '*.old' -o -name '.okayspace-setup*' \) 2>/dev/null | head -3 | tr '\n' ' ')"
  [ -z "$n" ]; check $? "$2: no half-finished files (.old, staging folder)" "found: $n"
}
launcher_starts() { # the installed launcher can still be started
  start_program "$INST_W\\OkaySpace.exe"; check $? "$1: the launcher still starts"; stop_bg
}

# ---- scenarios ------------------------------------------------------------------------------------------------------------
sc_fresh() {
  echo "== S1 fresh install"
  reset
  setup "$T/Setup-9.9.9.exe" /S
  expect_rc 0 "install succeeds"
  dist_installed "$T/d1" "$INST" "every file of the distribution is installed, byte for byte"
  has_file "$INST/Uninstall.exe" "the uninstaller is installed"
  no_leftovers "$INST" "after install"
  has_file "$SMDIR/OkaySpace/OkaySpace.lnk" "a Start Menu folder with the OkaySpace shortcut"
  has_file "$SMDIR/OkaySpace/Uninstall OkaySpace.lnk" "the Start Menu folder has an uninstall shortcut"
  has_file "$DESK/OkaySpace.lnk" "the Desktop shortcut is made on the first install"
  strings -el "$SMDIR/OkaySpace/OkaySpace.lnk" 2>/dev/null | grep -qF "$INST_W"; check $? "the shortcut starts in the install folder" "Start in is not $INST_W"
  local a k; a="$(regdump "$ARP")"; k="$(regdump "$APPKEY")"
  [ "$(regget "$a" DisplayName)" = "OkaySpace" ]; check $? "Apps & features: name"
  [ "$(regget "$a" DisplayVersion)" = "9.9.9" ]; check $? "Apps & features: version"
  [ "$(regget "$a" Publisher)" = "OkaySpace" ]; check $? "Apps & features: publisher"
  [ "$(regget "$a" InstallLocation)" = "$INST_W" ]; check $? "Apps & features: install location" "got $(regget "$a" InstallLocation)"
  [ "$(regget "$a" UninstallString)" = "\"$INST_W\\Uninstall.exe\"" ]; check $? "Apps & features: the uninstall command is quoted" "got $(regget "$a" UninstallString)"
  [ "$(regget "$a" QuietUninstallString)" = "\"$INST_W\\Uninstall.exe\" /S" ]; check $? "Apps & features: a quiet uninstall command" "got $(regget "$a" QuietUninstallString)"
  local sz; sz="$(regdword "$a" EstimatedSize)"; [ -n "$sz" ] && [ "$((sz))" -gt 0 ]; check $? "Apps & features: the size is written" "got '$sz'"
  [ -n "$(regget "$a" InstallDate)" ]; check $? "Apps & features: the install date is written"
  [ -z "$(regget "$a" URLInfoAbout)" ] && [ -z "$(regget "$a" HelpLink)" ]; check $? "Apps & features: no support link (owner decision: none until there is a stable page)"
  [ "$(regget "$k" InstallDir)" = "$INST_W" ]; check $? "the install folder is remembered"
  [ "$(regdword "$k" DesktopShortcut)" = "0x1" ]; check $? "the Desktop shortcut choice is remembered (made = 1)" "got '$(regdword "$k" DesktopShortcut)'"
  LOGTEXT="$(slog)"
  [ -n "$LOGTEXT" ]; check $? "a Setup log is written to %TEMP%\\OkaySpace-setup.log"
  log_has "Setup started" "the log says Setup started"
  log_has "the folder can be created and written" "the log says the folder was checked"
  log_has "free space" "the log says free space was checked"
  log_has "no OkaySpace program is running" "the log says running programs were checked"
  log_has "Setup finished. Exit code 0." "the log ends with the exit code"
  ! printf '%s\n' "$LOGTEXT" | grep -q "WARNING"; check $? "a plain install into the default folder has no warnings"
}

sc_update() {
  echo "== S2 update over an install with the person's own files, edits and stale files"
  reset
  setup "$T/Setup-9.9.9.exe" /S
  expect_rc 0 "first install"
  # The person's own work, settings, an edited shipped file, and a Desktop shortcut they deleted.
  mkdir -p "$INST/Projects/MyGame/Assets" "$INST/Versions/2.850.1/Tools" "$INST/community" "$INST/downloads"
  printf 'project' > "$INST/Projects/MyGame/project.okayproj"; printf 'scene' > "$INST/Projects/MyGame/Assets/main.okayscene"
  printf 'projects_dir=custom' > "$INST/launcher.cfg"; printf 'cfg' > "$INST/notifications.cfg"
  printf 'editor' > "$INST/Versions/2.850.1/Tools/OkayEngine.exe"; printf 'zip' > "$INST/community/pack.zip"
  printf 'my own notes' > "$INST/notes.txt"
  printf 'my own texture' > "$INST/Assets/Textures/my_own_texture.png"
  printf 'edited by the person' >> "$INST/README.txt"; printf 'edited scene' >> "$INST/examples/vehicle/vehicle_test.okayscene"
  rm -f "$DESK/OkaySpace.lnk" "$DESK/OkaySpace.desktop"
  local user_before; user_before="$(cd "$INST" && sha256sum Projects/MyGame/project.okayproj Projects/MyGame/Assets/main.okayscene launcher.cfg notifications.cfg \
      Versions/2.850.1/Tools/OkayEngine.exe community/pack.zip notes.txt Assets/Textures/my_own_texture.png)"
  has_file "$INST/Tools/old_tool.exe" "(the old release shipped Tools\\old_tool.exe)"
  setup "$T/Setup-9.9.10.exe" /S
  expect_rc 0 "update succeeds"
  dist_installed "$T/d2" "$INST" "every file of the new release is installed, byte for byte (edits to shipped files are replaced)"
  local user_after; user_after="$(cd "$INST" && sha256sum Projects/MyGame/project.okayproj Projects/MyGame/Assets/main.okayscene launcher.cfg notifications.cfg \
      Versions/2.850.1/Tools/OkayEngine.exe community/pack.zip notes.txt Assets/Textures/my_own_texture.png)"
  [ "$user_before" = "$user_after" ]; check $? "projects, settings, Versions, downloads and the person's own files are untouched"
  has_file "$INST/Tools/new_tool.exe" "a file the new release adds is installed"
  no_file "$INST/Tools/old_tool.exe" "a file the new release no longer ships is removed"
  no_file "$INST/examples/vehicle/old_sample.okayscene" "a dropped example is removed"
  no_file "$DESK/OkaySpace.lnk" "the Desktop shortcut the person deleted is not made again"
  has_file "$SMDIR/OkaySpace/OkaySpace.lnk" "the Start Menu shortcut is there"
  no_leftovers "$INST" "after the update"
  [ "$(regget "$(regdump "$ARP")" DisplayVersion)" = "9.9.10" ]; check $? "Apps & features shows the new version"
  [ "$(tr -d '\r\n' < "$INST/VERSION.txt")" = "9.9.10" ]; check $? "VERSION.txt is the new version"
  LOGTEXT="$(slog)"
  log_has "Setup finished. Exit code 0." "the log ends with the exit code"
}

sc_older() {
  echo "== S3 an older Setup over a newer install"
  reset
  setup "$T/Setup-9.9.10.exe" /S
  expect_rc 0 "install the newer version"
  local before; before="$(snap "$INST")"
  setup "$T/Setup-9.9.8.exe" /S
  expect_nonzero "the older Setup refuses in silent mode"
  expect_rc 13 "the code says a newer version is installed"
  same_tree "$before" "$(snap "$INST")" "nothing was replaced"
  [ "$(regget "$(regdump "$ARP")" DisplayVersion)" = "9.9.10" ]; check $? "Apps & features still shows the newer version"
  LOGTEXT="$(slog)"
  log_has "9.9.10" "the log names the installed version"
  log_has "/FORCE" "the log says how to go ahead anyway"
  setup "$T/Setup-9.9.8.exe" /S /FORCE
  expect_rc 0 "with /FORCE it goes ahead"
  dist_installed "$T/d0" "$INST" "the older version is installed"
  setup "$T/Setup-9.9.8.exe" /S
  expect_rc 0 "the same version again is allowed (a repair)"
}

sc_running() {
  echo "== S4 a program of OkaySpace is running"
  local prog
  for prog in OkaySpace.exe Tools/OkayEngine.exe Tools/OkaySpacePlayer.exe Tools/okayspace-relay.exe Tools/okayui_d3d11_demo.exe; do
    reset
    setup "$T/Setup-9.9.9.exe" /S
    local before; before="$(snap "$INST")"
    start_program "$INST_W\\${prog//\//\\}"; check $? "($prog is running)"
    setup "$T/Setup-9.9.10.exe" /S
    expect_nonzero "Setup refuses while $prog runs"
    expect_rc 10 "the code says a program is running ($prog)"
    same_tree "$before" "$(snap "$INST")" "nothing was replaced ($prog running)"
    no_leftovers "$INST" "after the refusal ($prog)"
    LOGTEXT="$(slog)"
    log_has "${prog//\//\\}" "the log names the running program ($prog)"
    stop_bg
    setup "$T/Setup-9.9.10.exe" /S
    expect_rc 0 "after closing $prog the update goes through"
  done
}

sc_nofolder() {
  echo "== S5 a folder that cannot be created or written"
  reset
  printf 'a file in the way' > "$T/work/afile"
  setup "$T/Setup-9.9.9.exe" /S "/D=$(wp "$T/work/afile/OkaySpace")"
  expect_nonzero "Setup refuses a folder below a file"
  expect_rc 11 "the code says the folder cannot be used"
  nothing_installed "$LAD/OkaySpace" "below a file"
  LOGTEXT="$(slog)"
  log_has "FAILED" "the log says it failed"
  log_has "Exit code 11" "the log has the exit code"
  # A folder the person has no permission to write (needs a user that is not root: root writes anywhere).
  if [ "$(id -u)" != 0 ]; then
    reset
    mkdir -p "$T/work/locked"; chmod 555 "$T/work/locked"
    setup "$T/Setup-9.9.9.exe" /S "/D=$(wp "$T/work/locked/OkaySpace")"
    expect_rc 11 "Setup refuses a folder it has no permission to write"
    nothing_installed "$T/work/locked/OkaySpace" "no permission"
    LOGTEXT="$(slog)"; printf '%s\n' "$LOGTEXT" | grep -q -E 'Setup cannot (make|write)'; check $? "the log says Setup cannot make or write the folder" "$(printf '%s\n' "$LOGTEXT" | tail -2 | tr '\n' ' ')"
    chmod 755 "$T/work/locked"
  else
    echo "  skip a folder without permission (running as root, which can write anywhere)"
  fi
  # A read-only disk (needs root to mount one; skipped without).
  mkdir -p "$T/ro"
  if mount -t tmpfs -o size=4m tmpfs "$T/ro" 2>/dev/null; then
    MOUNTS="$MOUNTS $T/ro"
    mount -o remount,ro "$T/ro" 2>/dev/null
    reset
    setup "$T/Setup-9.9.9.exe" /S "/D=$(wp "$T/ro/OkaySpace")"
    expect_nonzero "Setup refuses a read-only disk"
    expect_rc 11 "the code says the folder cannot be used (read-only disk)"
    nothing_installed "$T/ro/OkaySpace" "read-only disk"
    stop_bg; umount "$T/ro" 2>/dev/null
  else
    echo "  skip a read-only disk (cannot mount a tmpfs here)"
  fi
}

# Mount a small RAM disk of the given size for a scenario.
tiny_disk() { # tiny_disk <name> <size>
  mkdir -p "$T/$1"
  mount -t tmpfs -o "size=$2" tmpfs "$T/$1" 2>/dev/null || return 1
  MOUNTS="$MOUNTS $T/$1"
}
free_kb() { df -k --output=avail "$1" | tail -1 | tr -d ' '; }

sc_fulldisk() {
  echo "== S6 a full disk"
  if ! tiny_disk tiny 22m; then echo "  skip the full disk scenarios (cannot mount a tmpfs here)"; return; fi
  local dir_w; dir_w="$(wp "$T/tiny/OkaySpace")"
  # (a) an update that does not fit: the old folder must stay whole and working.
  reset
  setup "$T/Setup-big-9.9.9.exe" /S "/D=$dir_w"
  expect_rc 0 "install 9.9.9 on the small disk"
  INST_W="$dir_w"; INST="$T/tiny/OkaySpace"
  local before; before="$(snap "$INST")"
  setup "$T/Setup-big-9.9.10.exe" /S
  expect_nonzero "the update that does not fit is refused or rolled back"
  same_tree "$before" "$(snap "$INST")" "the old folder is exactly as it was"
  no_leftovers "$INST" "after the failed update"
  launcher_starts "after the failed update"
  [ "$(regget "$(regdump "$ARP")" DisplayVersion)" = "9.9.9" ]; check $? "Apps & features still shows the old version"
  LOGTEXT="$(slog)"
  log_has "Exit code" "the log has an exit code line"
  # (b) the disk fills during unpacking (the space check is turned down for this run): rolled back, old folder whole.
  # Leave about 3 MB free: enough to pass the lowered check, not enough for the unpacked files.
  local fill=$(( $(free_kb "$T/tiny") - 3000 ))
  [ "$fill" -gt 0 ] && head -c "$((fill * 1024))" /dev/zero > "$T/tiny/filler.bin"
  before="$(snap "$INST")"
  setup "$T/Setup-big-9.9.10-nomargin.exe" /S
  expect_nonzero "a disk that fills during unpacking fails"
  # snap includes filler.bin only when under INST, so it is not part of the comparison.
  same_tree "$before" "$(snap "$INST")" "the old folder is exactly as it was after the disk filled"
  no_leftovers "$INST" "after the disk filled"
  launcher_starts "after the disk filled"
  LOGTEXT="$(slog)"
  log_has "Exit code" "the log has an exit code line (disk filled)"
  rm -f "$T/tiny/filler.bin"
  # (c) a fresh install that does not fit: nothing is left behind.
  rm -rf "$T/tiny/OkaySpace"
  reset
  local fill2=$(( $(free_kb "$T/tiny") - 2500 ))
  [ "$fill2" -gt 0 ] && head -c "$((fill2 * 1024))" /dev/zero > "$T/tiny/filler.bin"
  setup "$T/Setup-big-9.9.9.exe" /S "/D=$dir_w"
  expect_nonzero "a fresh install on a disk that is too small fails"
  nothing_installed "$T/tiny/OkaySpace" "too small disk"
  local left; left="$(find "$T/tiny/OkaySpace" -type f 2>/dev/null | head -3 | tr '\n' ' ')"
  [ -z "$left" ]; check $? "no half-written files are left on the disk" "found: $left"
  stop_bg; umount "$T/tiny" 2>/dev/null
  INST_W="$LAD_W\\OkaySpace"; INST="$LAD/OkaySpace"
}

sc_swapfail() {
  echo "== S6d the swap fails half way: everything is put back"
  reset
  setup "$T/Setup-9.9.9.exe" /S
  expect_rc 0 "install 9.9.9"
  # A folder named like the backup of one program makes the "move the old one aside" step fail for that file, after
  # earlier files (the launcher, SDL2.dll, ...) were already replaced.
  mkdir -p "$INST/Tools/okayui_d3d11_demo.exe.old"; printf 'not ours' > "$INST/Tools/okayui_d3d11_demo.exe.old/keep.txt"
  local before; before="$(snap "$INST")"
  setup "$T/Setup-9.9.10.exe" /S
  expect_nonzero "Setup reports the failed swap"
  expect_rc 15 "the code says the new files could not be put in place"
  same_tree "$before" "$(snap "$INST")" "every old file is back, byte for byte (the planted folder is untouched)"
  no_file "$INST/Tools/new_tool.exe" "a file that was only in the new release is not left behind"
  no_file "$INST/.okayspace-setup-new" "the staging folder is gone"
  launcher_starts "after the rollback"
  [ "$(regget "$(regdump "$ARP")" DisplayVersion)" = "9.9.9" ]; check $? "Apps & features still shows the old version"
  LOGTEXT="$(slog)"
  log_has "FAILED" "the log says it failed"
  log_has "restored" "the log says the old files were restored"
}

# The audit's "after use" tree, made by hand with the names and places the programs use (docs/install_audit.md, file map).
simulate_use() { # simulate_use  (expects an install in $INST)
  local I="$INST" A="$APPD/OkaySpace"
  mkdir -p "$I/Tools/Projects/EditorProject/Assets" "$I/Projects/MyGame/Assets" "$I/community" "$I/downloads" \
           "$I/Games/Official/Data" "$I/Games/Official/.okayofficial" "$I/Games/Official/Saves" \
           "$I/Versions/2.850.1/Tools/Projects/InVersion/Assets" "$I/Versions/2.700.0/Tools" \
           "$I/Versions/.partial-2.860.0-1/Tools" "$I/Versions/.removing-2.600.0-1" "$I/examples/my_own_scene" "$A/ipc" "$TMPW/okayspace_media" "$TMPW/okayspace_official_1"
  local f
  # leftovers the programs wrote (removed always)
  for f in okay_update.log okay_selfupdate.state OkaySpace.exe.old OkaySpace.exe.new OkaySpace.exe.failed \
           Tools/okay_update.log Tools/okay_crash.txt Tools/okay_crashlog.txt Tools/okayspace-relay.exe.old Tools/OkayEngine.exe.new \
           Tools/okay_ipc_request.txt Tools/okay_ipc_ack.txt; do printf 'x' > "$I/$f"; done
  # editors the Versions page installed (removed, except a folder that holds a project)
  for f in Versions/2.850.1/Tools/OkayEngine.exe Versions/2.850.1/Tools/OkaySpacePlayer.exe Versions/2.850.1/Tools/SDL2.dll \
           Versions/2.850.1/install.txt Versions/2.850.1/Tools/okay_crash.txt Versions/2.700.0/Tools/OkayEngine.exe \
           Versions/2.700.0/Tools/imgui.ini Versions/.partial-2.860.0-1/Tools/OkayEngine.exe.part Versions/.removing-2.600.0-1/leftover.bin; do printf 'x' > "$I/$f"; done
  printf 'engine_version=2.850.1\n' > "$I/Versions/2.850.1/Tools/Projects/InVersion/project.okayproj"
  printf 'scene' > "$I/Versions/2.850.1/Tools/Projects/InVersion/Assets/level.okayscene"
  # settings and state (kept unless the box is ticked)
  for f in launcher.cfg notifications.cfg news.cfg scheduled.cfg chat_seen_alice.cfg imgui.ini okay_recent.txt okay_settings.txt okay_layouts.txt \
           okay_projectview.txt okay_custom_actions.txt achievements.txt playtime.txt profile_showcase.txt account_server.txt Tools/plugins_disabled.txt; do
    printf 'x' > "$I/$f"; done
  # projects and downloads (kept unless the box is ticked)
  printf 'engine_version=2.870.0\n' > "$I/Projects/MyGame/project.okayproj"; printf 'scene' > "$I/Projects/MyGame/Assets/main.okayscene"
  printf 'engine_version=2.870.0\n' > "$I/Tools/Projects/EditorProject/project.okayproj"; printf 'scene' > "$I/Tools/Projects/EditorProject/Assets/e.okayscene"
  printf 'zip' > "$I/community/pack1.zip"; printf 'zip' > "$I/downloads/mod.zip"; printf 'part' > "$I/downloads/big.zip.part"
  # the official game and a save it wrote beside itself (kept unless the box is ticked)
  printf 'MZ' > "$I/Games/Official/Game.exe"; printf 'cfg' > "$I/Games/Official/Data/game.okayconfig"
  printf '{}' > "$I/Games/Official/.okayofficial/manifest.json"; printf 'save' > "$I/Games/Official/Saves/slot1.sav"
  # the person's own files in the folders Setup also uses (never removed)
  printf 'mine' > "$I/Assets/Fonts/my_font.ttf"; printf 'mine' > "$I/Assets/Textures/my_own_texture.png"
  printf 'mine' > "$I/examples/my_own_scene/scene.okayscene"; printf 'mine' > "$I/notes.txt"
  # the roaming profile: the sign-in token goes always; accounts and the server list are settings
  printf 'token' > "$A/session.txt"; printf 'game' > "$A/ipc/game-1234.json"; printf 'db' > "$A/accounts.db"; printf 'servers' > "$A/servers.cfg"
  # temp files
  printf 'x' > "$TMPW/okayspace_release.json"; printf 'x' > "$TMPW/okayspace_ver_1.txt"; printf 'x' > "$TMPW/okayspace_media/a.png"; printf 'x' > "$TMPW/okayspace_official_1/b.bin"
}
tree_of() { (cd "$1" 2>/dev/null && find . -type f | LC_ALL=C sort); }
list_equal() { # list_equal <expected text> <actual text> <message>
  local exp; exp="$(printf '%s\n' "$1" | LC_ALL=C sort)"; set -- "$exp" "$2" "$3"
  if [ "$1" = "$2" ]; then check 0 "$3"; else check 1 "$3" "$(diff <(printf '%s\n' "$1") <(printf '%s\n' "$2") | head -12 | tr '\n' ';')"; fi
}
EXPECT_KEPT_OFF='./Assets/Fonts/my_font.ttf
./Assets/Textures/my_own_texture.png
./Games/Official/.okayofficial/manifest.json
./Games/Official/Data/game.okayconfig
./Games/Official/Game.exe
./Games/Official/Saves/slot1.sav
./Projects/MyGame/Assets/main.okayscene
./Projects/MyGame/project.okayproj
./Tools/Projects/EditorProject/Assets/e.okayscene
./Tools/Projects/EditorProject/project.okayproj
./Tools/plugins_disabled.txt
./Versions/2.850.1/Tools/Projects/InVersion/Assets/level.okayscene
./Versions/2.850.1/Tools/Projects/InVersion/project.okayproj
./account_server.txt
./achievements.txt
./chat_seen_alice.cfg
./community/pack1.zip
./downloads/big.zip.part
./downloads/mod.zip
./examples/my_own_scene/scene.okayscene
./imgui.ini
./launcher.cfg
./news.cfg
./notes.txt
./notifications.cfg
./okay_custom_actions.txt
./okay_layouts.txt
./okay_projectview.txt
./okay_recent.txt
./okay_settings.txt
./playtime.txt
./profile_showcase.txt
./scheduled.cfg'
EXPECT_KEPT_ON='./Assets/Fonts/my_font.ttf
./Assets/Textures/my_own_texture.png
./examples/my_own_scene/scene.okayscene
./notes.txt'

sc_uninstall_keep() {
  echo "== S7 uninstall, the box is off (keep my projects, settings and sign-in)"
  reset
  setup "$T/Setup-9.9.9.exe" /S
  expect_rc 0 "install"
  simulate_use
  uninstall_wait /S
  list_equal "$EXPECT_KEPT_OFF" "$(tree_of "$INST")" "what is left in the install folder is exactly the projects, settings, official game and the person's own files"
  no_file "$APPD/OkaySpace/session.txt" "the sign-in token is removed"
  no_file "$APPD/OkaySpace/ipc" "the game ipc folder is removed"
  has_file "$APPD/OkaySpace/accounts.db" "local accounts are kept (they are the person's)"
  has_file "$APPD/OkaySpace/servers.cfg" "the server list is kept"
  no_file "$INST/Versions/2.700.0" "an editor version without a project is removed"
  no_file "$INST/Versions/.partial-2.860.0-1" "scratch folders in Versions are removed"
  has_file "$INST/Versions/2.850.1/Tools/Projects/InVersion/project.okayproj" "a project inside a version folder is kept"
  [ -z "$(ls "$TMPW" | grep -i '^okayspace_')" ]; check $? "the temp files are removed"
  no_file "$SMDIR/OkaySpace" "the Start Menu folder is removed"
  no_file "$DESK/OkaySpace.lnk" "the Desktop shortcut is removed"
  [ -z "$(regdump "$ARP")" ]; check $? "the Apps & features entry is removed"
  [ -z "$(regdump "$APPKEY")" ]; check $? "the install folder registry key is removed"
  LOGTEXT="$(ulog)"
  log_has "Kept:" "the uninstall log lists what was kept"
  log_has "Versions" "the log names the kept project folder inside Versions"
  log_has "Projects" "the log names the Projects folder"
  log_has "Games" "the log names the official game folder"
  log_has "Uninstall finished. Exit code 0." "the log ends with the exit code"
}

sc_uninstall_all() {
  echo "== S8 uninstall, the box is ticked (also remove my projects, settings and sign-in)"
  reset
  setup "$T/Setup-9.9.9.exe" /S
  expect_rc 0 "install"
  simulate_use
  uninstall_wait /S /REMOVEUSERDATA
  list_equal "$EXPECT_KEPT_ON" "$(tree_of "$INST")" "only the person's own unknown files are left in the install folder"
  [ ! -d "$APPD/OkaySpace" ]; check $? "the roaming OkaySpace folder is gone (token, accounts, server list)" "$(ls "$APPD/OkaySpace" 2>/dev/null | tr '\n' ' ')"
  no_file "$INST/Projects" "the Projects folder is removed"
  no_file "$INST/Games" "the official game folder is removed"
  no_file "$INST/Versions" "Versions is removed including a project inside it"
  [ -z "$(regdump "$ARP")" ]; check $? "the Apps & features entry is removed"
  LOGTEXT="$(ulog)"
  log_has "Uninstall finished. Exit code 0." "the log ends with the exit code"
  # With nothing of the person's own left, the folder itself goes.
  reset
  setup "$T/Setup-9.9.9.exe" /S
  uninstall_wait /S
  no_file "$INST" "a clean install and uninstall leaves no folder behind"
  [ ! -d "$APPD/OkaySpace" ]; check $? "and no roaming folder"
  no_file "$SMDIR/OkaySpace" "and no Start Menu folder"
}

sc_uninstall_running() {
  echo "== S9 uninstall while the launcher is open"
  reset
  setup "$T/Setup-9.9.9.exe" /S
  simulate_use
  local before; before="$(snap "$INST")"
  start_program "$INST_W\\OkaySpace.exe"; check $? "(the launcher is running)"
  uninstall_sync /S
  expect_nonzero "uninstall refuses while the launcher runs"
  expect_rc 10 "the code says a program is running"
  same_tree "$before" "$(snap "$INST")" "nothing was removed"
  has_file "$APPD/OkaySpace/session.txt" "the sign-in token is still there (nothing was done)"
  [ -n "$(regdump "$ARP")" ]; check $? "the Apps & features entry is still there, so uninstall can be run again"
  has_file "$SMDIR/OkaySpace/OkaySpace.lnk" "the shortcuts are still there"
  LOGTEXT="$(ulog)"
  log_has "OkaySpace.exe" "the log names the running program"
  stop_bg
  # A kept editor from the Versions page that is running blocks it too.
  start_program "$INST_W\\Tools\\OkayEngine.exe"; check $? "(the editor is running)"
  uninstall_sync /S
  expect_rc 10 "uninstall refuses while the editor runs"
  stop_bg
  # ...and so does a program inside Versions (the uninstaller looks there too).
  cp "$T/sleeper.exe" "$INST/Versions/2.850.1/Tools/OkayEngine.exe"
  start_program "$INST_W\\Versions\\2.850.1\\Tools\\OkayEngine.exe"; check $? "(a kept editor in Versions is running)"
  uninstall_sync /S
  expect_rc 10 "uninstall refuses while a kept editor in Versions runs"
  LOGTEXT="$(ulog)"
  log_has 'Versions\2.850.1\Tools\OkayEngine.exe' "the log names the kept editor"
  stop_bg
}

sc_oddfolders() {
  echo "== S10 folders with spaces, accented and non-Latin letters, OneDrive, Program Files, very long paths"
  reset
  local odd='C:\Test Space\Ünï Ökay 日本\OkaySpace'
  setup_cmd "$(wp "$T/Setup-9.9.9.exe") /S /D=$odd"
  expect_rc 0 "install into a folder with spaces and non-Latin letters"
  has_file "$PFX/drive_c/Test Space/Ünï Ökay 日本/OkaySpace/OkaySpace.exe" "the files are there"
  LOGTEXT="$(slog)"
  log_has "WARNING" "the log warns"
  log_has "code page" "the warning is about characters outside the Windows ANSI code page"
  INST_W="$odd"; INST="$PFX/drive_c/Test Space/Ünï Ökay 日本/OkaySpace"
  uninstall_wait /S
  no_file "$PFX/drive_c/Test Space/Ünï Ökay 日本/OkaySpace/OkaySpace.exe" "uninstall works there too"
  INST_W="$LAD_W\\OkaySpace"; INST="$LAD/OkaySpace"
  rm -rf "$PFX/drive_c/Test Space"
  reset
  setup_cmd "$(wp "$T/Setup-9.9.9.exe") /S /D=C:\\Users\\Public\\OneDrive\\OkaySpace"
  expect_rc 0 "install into a folder named OneDrive goes ahead in silent mode"
  LOGTEXT="$(slog)"; log_has "OneDrive" "the log warns about OneDrive"
  rm -rf "$PFX/drive_c/users/Public/OneDrive"
  reset
  setup_cmd "$(wp "$T/Setup-9.9.9.exe") /S /D=C:\\Program Files\\OkaySpace"
  LOGTEXT="$(slog)"
  if [ "$RC" = 0 ]; then log_has "Program Files" "the log warns about Program Files"; else expect_rc 11 "or refuses a folder it cannot write (Program Files)"; fi
  rm -rf "$PFX/drive_c/Program Files/OkaySpace"
  reset
  local long="C:\\Users\\Public\\$(printf 'a%.0s' $(seq 1 90))\\$(printf 'b%.0s' $(seq 1 90))\\OkaySpace"
  setup_cmd "$(wp "$T/Setup-9.9.9.exe") /S /D=$long"
  expect_rc 0 "install into a very long folder name goes ahead in silent mode"
  LOGTEXT="$(slog)"; log_has "long" "the log warns that the path is long"
  rm -rf "$PFX/drive_c/users/Public/$(printf 'a%.0s' $(seq 1 90))"
}

sc_generator() {
  echo "== S11 the file list generator (scripts/gen-installer-filelist.py)"
  if [ ! -f "$GEN" ]; then check 1 "scripts/gen-installer-filelist.py exists"; return; fi
  python3 -I "$GEN" "$T/d2" "$T/list.nsh" >"$T/gen.txt" 2>&1; check $? "it makes a list from a distribution folder" "$(head -3 "$T/gen.txt" | tr '\n' ' ')"
  grep -qF 'Tools\okayui_d3d11_demo.exe' "$T/list.nsh"; check $? "every program is in the list (Tools\\okayui_d3d11_demo.exe)"
  grep -qF 'examples\vehicle\vehicle_test.okayscene' "$T/list.nsh"; check $? "every example file is in the list"
  python3 -I "$GEN" "$T/d2" "$T/list2.nsh" >/dev/null 2>&1; cmp -s "$T/list.nsh" "$T/list2.nsh"; check $? "the same folder gives the same list (no dates, no order changes)"
  rm -rf "$T/dx"; cp -r "$T/d2" "$T/dx"; rm "$T/dx/Tools/OkayEngine.exe"
  python3 -I "$GEN" "$T/dx" "$T/listx.nsh" >/dev/null 2>&1; [ $? -ne 0 ]; check $? "a distribution without the editor is refused"
  rm -rf "$T/dx"; cp -r "$T/d2" "$T/dx"; : > "$T/dx/OkaySpace.exe"
  python3 -I "$GEN" "$T/dx" "$T/listx.nsh" >/dev/null 2>&1; [ $? -ne 0 ]; check $? "an empty launcher is refused"
  rm -rf "$T/dx"; cp -r "$T/d2" "$T/dx"; printf 'x' > "$T/dx/examples/bad\$name.txt"
  python3 -I "$GEN" "$T/dx" "$T/listx.nsh" >/dev/null 2>&1; [ $? -ne 0 ]; check $? "a file name with a dollar sign is refused (it would be read as an installer variable)"
  rm -rf "$T/dx"; cp -r "$T/d2" "$T/dx"; ln -s /etc/passwd "$T/dx/examples/link.txt"
  python3 -I "$GEN" "$T/dx" "$T/listx.nsh" >/dev/null 2>&1; [ $? -ne 0 ]; check $? "a link in the distribution is refused"
  rm -rf "$T/dx"; cp -r "$T/d2" "$T/dx"
  python3 -I "$GEN" "$T/dx" --check >/dev/null 2>&1; check $? "--check validates without writing a list"
  rm -rf "$T/dx"
}


# An install made by an older Setup (no list of files, a loose Start Menu shortcut, no remembered Desktop choice).
old_style_install() { # old_style_install <dist> <version>
  mkdir -p "$INST"; cp -r "$1"/. "$INST"/
  W reg add "$APPKEY" /v InstallDir /t REG_SZ /d "$INST_W" /f >/dev/null 2>&1
  W reg add "$ARP" /v DisplayName /t REG_SZ /d OkaySpace /f >/dev/null 2>&1
  W reg add "$ARP" /v DisplayVersion /t REG_SZ /d "$2" /f >/dev/null 2>&1
  W reg add "$ARP" /v InstallLocation /t REG_SZ /d "$INST_W" /f >/dev/null 2>&1
  W reg add "$ARP" /v UninstallString /t REG_SZ /d "$INST_W\\Uninstall.exe" /f >/dev/null 2>&1
  printf 'old uninstaller' > "$INST/Uninstall.exe"
  printf 'lnk' > "$SMDIR/OkaySpace.lnk"
}
sc_transition() {
  echo "== S12 an update over an install made by the older Setup"
  reset
  old_style_install "$T/d1" 9.9.9
  printf 'lnk' > "$DESK/OkaySpace.lnk"
  setup "$T/Setup-9.9.10.exe" /S
  expect_rc 0 "update succeeds"
  dist_installed "$T/d2" "$INST" "the new release is installed"
  no_file "$SMDIR/OkaySpace.lnk" "the loose Start Menu shortcut of the older Setup is replaced"
  has_file "$SMDIR/OkaySpace/OkaySpace.lnk" "by the Start Menu folder"
  has_file "$DESK/OkaySpace.lnk" "the Desktop shortcut that was there stays"
  [ "$(regdword "$(regdump "$APPKEY")" DesktopShortcut)" = "0x1" ]; check $? "and is remembered as made"
  [ "$(regget "$(regdump "$ARP")" UninstallString)" = "\"$INST_W\\Uninstall.exe\"" ]; check $? "the uninstall command is now quoted"
  has_file "$INST/Tools/old_tool.exe" "a file dropped by the new release stays when there was no list from the older Setup (known limit)"
  has_file "$INST/installed-files.txt" "the list of files is written, so the next update can remove dropped files"
  # The person had deleted the Desktop shortcut before the first update with the new Setup: it must not come back.
  reset
  old_style_install "$T/d1" 9.9.9
  setup "$T/Setup-9.9.10.exe" /S
  expect_rc 0 "update succeeds (no Desktop shortcut before)"
  no_file "$DESK/OkaySpace.lnk" "no Desktop shortcut is made on an update"
  [ "$(regdword "$(regdump "$APPKEY")" DesktopShortcut)" = "0x0" ]; check $? "and the choice is remembered as not made"
}

sc_tamper() {
  echo "== S13 the list of files is only a plain file: it must not be able to delete anything else"
  reset
  setup "$T/Setup-9.9.9.exe" /S
  mkdir -p "$INST/Projects/MyGame" "$INST/Versions/2.850.1/Tools" "$INST/community"
  printf 'v' > "$LAD/victim_outside.txt"; printf 'v' > "$INST/../../victim_two_up.txt"
  printf 'p' > "$INST/Projects/MyGame/project.okayproj"; printf 'e' > "$INST/Versions/2.850.1/Tools/OkayEngine.exe"
  printf 'c' > "$INST/community/pack.zip"; printf 'n' > "$INST/notes.txt"; printf 'a' > "$INST/a.txt"
  mkdir -p "$INST/Tools/Projects/EditorProj/Assets"; printf 'p' > "$INST/Tools/Projects/EditorProj/project.okayproj"
  { printf '..\\victim_outside.txt\r\n..\\..\\victim_two_up.txt\r\nProjects\\MyGame\\project.okayproj\r\nVersions\\2.850.1\\Tools\\OkayEngine.exe\r\n'
    printf 'community\\pack.zip\r\nC:\\victim_drive.txt\r\n/etc/passwd\r\n*.txt\r\n?.txt\r\nTools\\..\\..\\victim_outside.txt\r\n'
    printf 'notes.txt\r\nTools\\Projects\\EditorProj\\project.okayproj\r\n'; } >> "$INST/installed-files.txt"
  printf 'v' > "$PFX/drive_c/victim_drive.txt"
  local keep="$(cd "$INST" && sha256sum Projects/MyGame/project.okayproj Versions/2.850.1/Tools/OkayEngine.exe community/pack.zip a.txt Tools/Projects/EditorProj/project.okayproj) $(sha256sum "$LAD/victim_outside.txt" "$PFX/drive_c/victim_drive.txt")"
  setup "$T/Setup-9.9.10.exe" /S
  expect_rc 0 "the update runs"
  local after="$(cd "$INST" && sha256sum Projects/MyGame/project.okayproj Versions/2.850.1/Tools/OkayEngine.exe community/pack.zip a.txt Tools/Projects/EditorProj/project.okayproj) $(sha256sum "$LAD/victim_outside.txt" "$PFX/drive_c/victim_drive.txt")"
  [ "$keep" = "$after" ]; check $? "an update does not remove files outside the install folder, the person's projects, Versions or downloads, or by wildcard"
  no_file "$INST/Tools/old_tool.exe" "(a real dropped file is still removed)"
  uninstall_wait /S
  after="$(cd "$INST" && sha256sum Projects/MyGame/project.okayproj Versions/2.850.1/Tools/OkayEngine.exe community/pack.zip a.txt 2>/dev/null) $(sha256sum "$LAD/victim_outside.txt" "$PFX/drive_c/victim_drive.txt")"
  # Versions/ is swept by the uninstaller on purpose (it holds program files); everything else must be there.
  [ -f "$LAD/victim_outside.txt" ] && [ -f "$PFX/drive_c/victim_drive.txt" ] && [ -f "$INST/Projects/MyGame/project.okayproj" ] && [ -f "$INST/community/pack.zip" ] && [ -f "$INST/a.txt" ]
  check $? "uninstall removes nothing outside the install folder and none of the person's files because of a bad list"
  rm -f "$LAD/victim_outside.txt" "$LAD/../victim_two_up.txt" "$PFX/drive_c/victim_drive.txt"
}

sc_recover() {
  echo "== S14 an earlier Setup was killed half way through the swap"
  reset
  setup "$T/Setup-9.9.9.exe" /S
  expect_rc 0 "install 9.9.9"
  local before; before="$(snap "$INST")"
  # What a killed Setup leaves: the staging folder with its marker, two programs already replaced (old ones as .old).
  mkdir -p "$INST/.okayspace-setup-new"; printf 'swapping' > "$INST/.okayspace-setup-new/.swap-started"
  mv "$INST/OkaySpace.exe" "$INST/OkaySpace.exe.old"; printf 'half-new launcher' > "$INST/OkaySpace.exe"
  mv "$INST/Tools/SDL2.dll" "$INST/Tools/SDL2.dll.old"; printf 'half-new dll' > "$INST/Tools/SDL2.dll"
  setup "$T/Setup-9.9.9.exe" /S
  expect_rc 0 "Setup runs again"
  same_tree "$before" "$(snap "$INST")" "the folder is whole again, byte for byte (the old files were put back, then the same version was installed)"
  no_leftovers "$INST" "after recovery"
  LOGTEXT="$(slog)"
  log_has "unfinished Setup" "the log says an unfinished Setup was found"
  log_has "restored 2 old files" "and how many files it put back"
  # A staging folder without the marker is only scratch: removed, nothing restored.
  mkdir -p "$INST/.okayspace-setup-new/Tools"; printf 'x' > "$INST/.okayspace-setup-new/Tools/partial.exe"
  setup "$T/Setup-9.9.9.exe" /S
  expect_rc 0 "Setup runs again (scratch staging folder)"
  no_leftovers "$INST" "after removing a scratch staging folder"
}

sc_dswitch() {
  echo "== S15 /D= with an install already there"
  reset
  setup "$T/Setup-9.9.9.exe" /S
  setup "$T/Setup-9.9.10.exe" /S "/D=$(wp "$T/work/second")"
  expect_rc 0 "Setup runs"
  dist_installed "$T/d2" "$INST" "the existing install is the one that was updated"
  no_file "$T/work/second" "no second copy was made"
  LOGTEXT="$(slog)"
  log_has "/D=" "the log says /D= was not used and why"
}

sc_link() {
  echo "== S16 uninstall does not follow a link out of the install folder"
  reset
  setup "$T/Setup-9.9.9.exe" /S
  mkdir -p "$INST/Versions/2.850.1/Tools" "$T/work/outside/deep"
  printf 'precious' > "$T/work/outside/precious.txt"; printf 'precious' > "$T/work/outside/deep/precious2.txt"
  ln -s "$T/work/outside" "$INST/Versions/2.850.1/Tools/link"
  uninstall_wait /S
  [ -f "$T/work/outside/precious.txt" ] && [ -f "$T/work/outside/deep/precious2.txt" ]
  check $? "files the link pointed at are still there"
  # The top-level folders as links: Versions put on another drive with a junction (docs/install_audit.md, plan item 12), and a Projects folder kept elsewhere.
  reset
  setup "$T/Setup-9.9.9.exe" /S
  mkdir -p "$T/work/otherdrive/2.850.1/Tools" "$T/work/myprojects/Game/Assets"
  printf 'editor' > "$T/work/otherdrive/2.850.1/Tools/OkayEngine.exe"; printf 'not ours' > "$T/work/otherdrive/readme.txt"
  printf 'proj' > "$T/work/myprojects/Game/project.okayproj"
  rm -rf "$INST/Versions"; ln -s "$T/work/otherdrive" "$INST/Versions"; ln -s "$T/work/myprojects" "$INST/Projects"
  uninstall_wait /S /REMOVEUSERDATA
  [ -f "$T/work/otherdrive/readme.txt" ] && [ -f "$T/work/otherdrive/2.850.1/Tools/OkayEngine.exe" ] && [ -f "$T/work/myprojects/Game/project.okayproj" ]
  check $? "folders that Versions and Projects were linked to are not emptied, even with the box ticked"
  # (Not checked: that the links themselves are gone. On Windows RMDir removes a junction or folder symbolic link; this wine cannot remove a
  # symbolic link to a folder with either RemoveDirectory or DeleteFile, so the links stay here and the log says so.)
}

# The build checks need no wine: the installer is built the way the release job builds it (the 32-bit target, no -X), and from a
# list made beforehand with -DFILELIST.
sc_build() {
  echo "== S0 the installer builds the way the release job builds it"
  local out="$T/Setup-release-style.exe" out2="$T/Setup-filelist.exe"
  rm -f "$out" "$out2"
  makensis -V1 -DVERSION=9.9.10 -DDISTDIR="$T/d2" -DOUTFILE="$out" "$NSI" >"$T/makensis.txt" 2>&1; check $? "makensis builds it (32-bit, as the release job does)" "$(head -3 "$T/makensis.txt" | tr '\n' ' ')"
  [ "$(head -c2 "$out" 2>/dev/null)" = "MZ" ] && [ "$(stat -c %s "$out" 2>/dev/null || echo 0)" -gt 100000 ]; check $? "the result is a Windows program of a sensible size"
  if [ -f "$GEN" ]; then
    python3 -I "$GEN" "$T/d2" "$T/prebuilt.nsh" >/dev/null 2>&1
    makensis -V1 -DVERSION=9.9.10 -DDISTDIR="$T/d2" -DFILELIST="$T/prebuilt.nsh" -DOUTFILE="$out2" "$NSI" >"$T/makensis2.txt" 2>&1; check $? "it also builds from a list made beforehand (-DFILELIST)" "$(head -3 "$T/makensis2.txt" | tr '\n' ' ')"
    [ -s "$out2" ]; check $? "and writes the installer"
  fi
}

sc_relative() {
  echo "== S17 a folder that is not a full path, and an Uninstall.exe in the wrong folder"
  reset
  setup "$T/Setup-9.9.9.exe" /S "/D=relative\\folder"
  expect_rc 11 "a folder without a drive letter is refused"
  nothing_installed "$LAD/OkaySpace" "relative folder"
  LOGTEXT="$(slog)"; log_has "not a full folder name" "the log says why"
  # Uninstall.exe copied to another folder must not clean that folder.
  reset
  setup "$T/Setup-9.9.9.exe" /S
  mkdir -p "$T/work/elsewhere"; cp "$INST/Uninstall.exe" "$T/work/elsewhere/Uninstall.exe"
  printf 'mine' > "$T/work/elsewhere/README.txt"; printf 'mine' > "$T/work/elsewhere/VERSION.txt"; printf 'mine' > "$T/work/elsewhere/SDL2.dll"
  W "$(wp "$T/work/elsewhere/Uninstall.exe")" /S "_?=$(wp "$T/work/elsewhere")" >"$T/out.txt" 2>&1; RC=$?
  note_run "Uninstall.exe in the wrong folder"
  expect_rc 17 "it refuses (not an OkaySpace folder)"
  [ -f "$T/work/elsewhere/README.txt" ] && [ -f "$T/work/elsewhere/VERSION.txt" ] && [ -f "$T/work/elsewhere/SDL2.dll" ]
  check $? "the files in that folder are untouched"
  [ -n "$(regdump "$ARP")" ]; check $? "and the real install is still registered"
}

sc_unzipped() {
  echo "== S18 Setup run from inside an unzipped copy (the audit's S3)"
  reset
  local z="$PFX/drive_c/OkaySpace-unzipped"; rm -rf "$z"; mkdir -p "$z"; cp -r "$T/d1"/. "$z"/; cp "$T/Setup-9.9.10.exe" "$z/Setup.exe"
  setup "C:\\OkaySpace-unzipped\\Setup.exe" /S
  expect_rc 0 "Setup updates the unzipped copy in place"
  dist_installed "$T/d2" "$z" "the unzipped folder now holds the new release"
  no_file "$INST/OkaySpace.exe" "no second copy was made in the default folder"
  [ "$(regget "$(regdump "$ARP")" InstallLocation)" = 'C:\OkaySpace-unzipped' ]; check $? "Apps & features points at the unzipped folder"
  no_leftovers "$z" "after updating the unzipped copy"
  has_file "$z/Setup.exe" "Setup itself, which sits in that folder, is untouched"
  INST_W='C:\OkaySpace-unzipped'; uninstall_wait /S; INST_W="$LAD_W\\OkaySpace"
  no_file "$z/Uninstall.exe" "the uninstaller removes itself from the unzipped folder too"
  [ -f "$z/Setup.exe" ]; check $? "(Setup.exe is not ours to remove: it is not in the list)"
  rm -rf "$z"
}

sc_custom() {
  echo "== S19 a folder of your own choice, with achievements in the default local folder"
  reset
  local dir_w='C:\Apps\OkaySpace' dir="$PFX/drive_c/Apps/OkaySpace"; rm -rf "$PFX/drive_c/Apps"
  setup "$T/Setup-9.9.9.exe" /S "/D=$dir_w"
  expect_rc 0 "install into C:\\Apps\\OkaySpace"
  mkdir -p "$LAD/OkaySpace"; printf 'a' > "$LAD/OkaySpace/achievements.txt"; printf 'p' > "$LAD/OkaySpace/playtime.txt"
  INST_W="$dir_w"; INST="$dir"
  uninstall_wait /S
  has_file "$LAD/OkaySpace/achievements.txt" "achievements in the default local folder are kept when the box is off"
  LOGTEXT="$(ulog)"; log_has "achievements and play time" "the log says so"
  no_file "$dir" "the chosen folder is gone"
  INST_W="$LAD_W\\OkaySpace"; INST="$LAD/OkaySpace"
  reset
  setup "$T/Setup-9.9.9.exe" /S "/D=$dir_w"
  mkdir -p "$LAD/OkaySpace"; printf 'a' > "$LAD/OkaySpace/achievements.txt"; printf 'p' > "$LAD/OkaySpace/playtime.txt"
  INST_W="$dir_w"; INST="$dir"
  uninstall_wait /S /REMOVEUSERDATA
  no_file "$LAD/OkaySpace" "with the box ticked they are removed, and the empty folder with them"
  INST_W="$LAD_W\\OkaySpace"; INST="$LAD/OkaySpace"
  rm -rf "$PFX/drive_c/Apps"
}

SCENARIOS="build generator fresh update older running nofolder fulldisk swapfail uninstall_keep uninstall_all uninstall_running oddfolders transition tamper recover dswitch link relative unzipped custom"
[ "$WINE_OK" = 1 ] || SCENARIOS="build generator"
for s in $SCENARIOS; do
  if [ -n "${OKAY_INSTALLER_ONLY:-}" ]; then case " $OKAY_INSTALLER_ONLY " in *" $s "*) ;; *) continue ;; esac; fi
  "sc_$s"
done

[ "$WINE_OK" = 1 ] && stop_bg
echo
if [ "$fail" != 0 ]; then echo "installer_wine: $nfail of $nchk checks FAILED"; exit 1; fi
if [ "$WINE_OK" != 1 ]; then
  echo "installer_wine: the $nchk build and file-list checks passed; the wine scenarios were SKIPPED ($WHY_NOT)"
  exit 77
fi
echo "installer_wine: all $nchk checks passed"
exit 0
