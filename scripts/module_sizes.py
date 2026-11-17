#!/usr/bin/env python3
"""Size of each engine module: source lines (always) and object-file bytes (when a build tree is given).

  scripts/module_sizes.py                   markdown table of lines per module
  scripts/module_sizes.py BUILD_DIR         adds code+data bytes of each module's .o files (uses `size`)
  scripts/module_sizes.py BUILD_DIR --json  machine readable (module_sizes.json)

An object file is counted for the module that owns its .cpp (scripts/check_modules.py ownership). Header-only
components are counted in the .cpp that includes them, so object bytes are a floor for header-heavy modules.
Not a link map: a real per-module link size needs the player target and comes with the build-per-module step.
"""
import json, os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_modules as cm


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    mods = cm.read_manifests()
    files = cm.source_files()
    owner = cm.build_owner(mods, files)
    lines = {n: 0 for n in mods}
    nfiles = {n: 0 for n in mods}
    for f in files:
        try:
            n = sum(1 for _ in open(os.path.join(cm.ENG, f), encoding='utf-8', errors='replace'))
        except OSError:
            continue
        lines[owner[f]] += n
        nfiles[owner[f]] += 1
    objbytes = {n: 0 for n in mods}
    if args:
        base = os.path.join(args[0], 'engine', 'CMakeFiles', 'okay_engine.dir')
        for dp, _, fs in os.walk(base):
            for f in fs:
                if not f.endswith('.o'):
                    continue
                rel = os.path.relpath(os.path.join(dp, f), base)[:-2]    # src/Core/X.cpp
                if rel not in set(files):
                    continue
                try:
                    out = subprocess.run(['size', os.path.join(dp, f)], capture_output=True, text=True).stdout.split('\n')[1].split()
                    objbytes[owner[rel]] += int(out[0]) + int(out[1])
                except Exception:
                    pass
    if '--json' in sys.argv:
        print(json.dumps({n: {'lines': lines[n], 'files': nfiles[n], 'object_bytes': objbytes[n]} for n in mods}, indent=1))
        return
    tot = sum(lines.values()) or 1
    print('| Module | Files | Lines | % of lines |' + (' Object code+data (KB) |' if args else ''))
    print('|---|---|---|---|' + ('---|' if args else ''))
    for n in sorted(mods, key=lambda k: -lines[k]):
        print(f'| {n} | {nfiles[n]} | {lines[n]} | {100.0 * lines[n] / tot:.1f}% |' + (f' {objbytes[n] // 1024} |' if args else ''))
    print(f'| **total** | {sum(nfiles.values())} | {tot} | |' + (f' {sum(objbytes.values()) // 1024} |' if args else ''))


main()
