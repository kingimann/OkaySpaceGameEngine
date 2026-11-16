#!/usr/bin/env python3
"""Make the list of files for the Windows installer from a distribution folder.

Why this exists: Setup and the uninstaller must know exactly which files OkaySpace Setup installs, so that

  * Setup can check every program (not just three) and every unpacked file before it changes anything,
  * the uninstaller removes those files and nothing else (a person's own files in examples\\ or Assets\\ stay),
  * an update can tell which files a newer release no longer ships.

The list is made from the folder that scripts/build-windows-dist.sh wrote (build-win-dist/), at the moment makensis
runs (scripts/okayspace-installer.nsi calls this script), so it always matches the bytes the installer carries,
including programs that were signed after the build.

Usage:
    gen-installer-filelist.py DISTDIR OUT.nsh     write the list for makensis
    gen-installer-filelist.py DISTDIR --check     only check the folder (used by build-windows-dist.sh)

Written from the NSIS script reference (macros, !define) and docs/install_audit.md. Standard library only. No outside code.

What it refuses (exit code 1, with a plain message): a missing or empty key program, a link, a file name with a
character that an installer script would read as something else (a dollar sign, a quote, a non-ASCII letter), a folder
that holds nothing. The point is that a build which could produce a Setup that "succeeds" with nothing useful installed
fails here, at build time.
"""
import os
import re
import sys

# Programs and files without which an install is useless. They must exist. (SDL2.dll is not on this list on purpose:
# a build with -DOKAY_STATIC_SDL=ON ships none, and our own platform layer will remove it. Any .exe, .dll or
# VERSION.txt that IS in the folder must not be empty, see validate().)
REQUIRED = [
    "OkaySpace.exe",
    "VERSION.txt",
    "Tools/OkayEngine.exe",
    "Tools/OkaySpacePlayer.exe",
]

# Windows will not create files with these names.
RESERVED = {"con", "prn", "aux", "nul"} | {"com%d" % i for i in range(1, 10)} | {"lpt%d" % i for i in range(1, 10)}

# Characters allowed in a file name. NSIS reads $ as a variable and " as the end of a string, so those can never be
# allowed; keeping to plain ASCII also keeps the script file free of encoding questions.
SAFE_NAME = re.compile(r"^[A-Za-z0-9 ._+()=,@-]+$")

MAX_PATH_CHARS = 200  # the installer adds the install folder on top; Windows stops at 260


class ListError(Exception):
    pass


def walk(dist):
    """Every regular file under dist as (relative path with '/', size), sorted. Raises ListError for anything odd."""
    if not os.path.isdir(dist):
        raise ListError("not a folder: %s" % dist)
    files = []
    for root, dirs, names in os.walk(dist, followlinks=False):
        dirs.sort()
        for d in dirs:
            full = os.path.join(root, d)
            if os.path.islink(full):
                raise ListError("%s is a link; the installer cannot carry links" % os.path.relpath(full, dist))
        for n in sorted(names):
            full = os.path.join(root, n)
            rel = os.path.relpath(full, dist).replace(os.sep, "/")
            if os.path.islink(full) or not os.path.isfile(full):
                raise ListError("%s is not a plain file (a link or a special file); the installer cannot carry it" % rel)
            files.append((rel, os.path.getsize(full)))
    files.sort(key=lambda f: f[0])
    return files


def validate(files):
    if not files:
        raise ListError("the distribution folder holds no files")
    by_path = dict(files)
    for need in REQUIRED:
        if need not in by_path:
            raise ListError("the distribution is incomplete: %s is missing" % need.replace("/", "\\"))
    for rel, size in files:
        low = rel.lower()
        if size == 0 and (low.endswith((".exe", ".dll")) or low in ("version.txt", "tools/version.txt")):
            raise ListError("the distribution is broken: %s is empty" % rel.replace("/", "\\"))
    seen = {}
    for rel, _size in files:
        if len(rel) > MAX_PATH_CHARS:
            raise ListError("%s is longer than %d characters" % (rel, MAX_PATH_CHARS))
        for part in rel.split("/"):
            if not SAFE_NAME.match(part) or part in (".", "..") or part.endswith(" ") or part.endswith("."):
                raise ListError(
                    "the file name %r is not allowed in the installer list (letters, digits, space and . _ - + ( ) = @ , only)" % rel)
            if part.split(".")[0].lower() in RESERVED:
                raise ListError("the file name %r is a name Windows does not allow" % rel)
        low = rel.lower()
        if low in seen:
            raise ListError("%s and %s differ only in capital letters; Windows treats them as one file" % (seen[low], rel))
        seen[low] = rel
        # Names the installer uses for its own bookkeeping must not come from the distribution.
        top = rel.split("/")[0].lower()
        if top in (".okayspace-setup-new",) or low in ("uninstall.exe", "installed-files.txt") or low.endswith(".old"):
            raise ListError("%s uses a name the installer keeps for itself" % rel)


def is_program(rel):
    return rel.lower().endswith(".exe")


def nsis_path(rel):
    return rel.replace("/", "\\")


def directories(files):
    dirs = set()
    for rel, _ in files:
        parts = rel.split("/")[:-1]
        for i in range(1, len(parts) + 1):
            dirs.add("/".join(parts[:i]))
    # deepest first, so an RMDir of each (which only removes an empty folder) can run in this order
    return sorted(dirs, key=lambda d: (-d.count("/"), d))


def render(files):
    total = sum(s for _, s in files)
    total_kb = (total + 1023) // 1024
    progs = [f for f in files if is_program(f[0])]
    out = []
    w = out.append
    w("; Generated by scripts/gen-installer-filelist.py -- do not edit. makensis makes it again each time it runs.")
    w("; %d files, %d programs, %d bytes." % (len(files), len(progs), total))
    w("!define DIST_FILE_COUNT %d" % len(files))
    w("!define DIST_PROGRAM_COUNT %d" % len(progs))
    w("!define DIST_TOTAL_BYTES %d" % total)
    w("!define DIST_TOTAL_KB %d" % total_kb)
    w("")
    w("; Every file: the macro gets (path relative to the install folder, size in bytes).")
    w("!macro EACH_DIST_FILE _M")
    for rel, size in files:
        w('  !insertmacro ${_M} "%s" %d' % (nsis_path(rel), size))
    w("!macroend")
    w("")
    w("; Every program (.exe) of the distribution: the macro gets (path, size).")
    w("!macro EACH_DIST_PROGRAM _M")
    for rel, size in progs:
        w('  !insertmacro ${_M} "%s" %d' % (nsis_path(rel), size))
    w("!macroend")
    w("")
    w("; Every folder of the distribution, deepest first: the macro gets the path.")
    w("!macro EACH_DIST_DIR _M")
    for d in directories(files):
        w('  !insertmacro ${_M} "%s"' % nsis_path(d))
    w("!macroend")
    w("")
    return "\n".join(out)


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    flags = [a for a in argv[1:] if a.startswith("--")]
    check_only = "--check" in flags
    unknown = [f for f in flags if f != "--check"]
    if unknown or not args or len(args) > 2 or (not check_only and len(args) != 2) or (check_only and len(args) != 1):
        sys.stderr.write("usage: gen-installer-filelist.py DISTDIR OUT.nsh | gen-installer-filelist.py DISTDIR --check\n")
        return 2
    dist = args[0]
    try:
        files = walk(dist)
        validate(files)
    except ListError as e:
        sys.stderr.write("installer file list: %s\n" % e)
        return 1
    total = sum(s for _, s in files)
    progs = sum(1 for f in files if is_program(f[0]))
    if not check_only:
        text = render(files)
        tmp = args[1] + ".tmp"
        with open(tmp, "w", newline="\n") as f:
            f.write(text)
        os.replace(tmp, args[1])
    print("installer file list: %d files (%d programs), %d bytes%s" % (len(files), progs, total, " (checked only)" if check_only else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
