#!/usr/bin/env python3
"""How many bytes of a linked program each engine module is (design PR 3, docs/engine/modular_engine.md).

  scripts/module_sizes.py BUILD_DIR MAP_FILE              markdown table
  scripts/module_sizes.py BUILD_DIR MAP_FILE --json OUT   also writes module_sizes.json to OUT

BUILD_DIR is a configured build folder (it has okay_modules.tsv, "module<TAB>engine file", written by
cmake/OkayModules.cmake). MAP_FILE is the GNU linker map of the program: configure with -DOKAY_LINK_MAP=ON and the
player writes bin/okay-player.map next to itself.

How it counts: every input section the linker kept (after the line "Linker script and memory map") is given to the
object file it came from. An object of the engine library (libokay_engine.a(Name.cpp.o)) goes to the module that owns
Name.cpp; when two engine files in different folders share that name and different modules own them, the bytes go to
"(same file name)". Other objects go to "program" (the program's own files) or to the library they came from. Debug
sections, notes and comments are not counted; .bss is counted separately because it takes no room in the file.
Written for this repository from the GNU ld map format (the "Linker script and memory map" listing); stdlib only.
"""
import json, os, re, sys

SKIP = ('.debug', '.comment', '.note', '.gnu.build.attributes', '.stab')
# " .text._ZN4okay3Foo  0x0000000000401000       0x2c engine/libokay_engine.a(Foo.cpp.o)" (name and numbers may sit on
# two lines when the section name is long)
ROW = re.compile(r'^\s*(\.\S+)?\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S.*)$')
NAME_ONLY = re.compile(r'^\s(\.\S+)\s*$')
MEMBER = re.compile(r'^(.*?)([^/\\]+)\(([^()]+)\)$')


def read_owners(build_dir):
    by_name = {}
    with open(os.path.join(build_dir, 'okay_modules.tsv'), encoding='utf-8') as f:
        for line in f:
            if '\t' not in line:
                continue
            mod, path = line.rstrip('\n').split('\t', 1)
            by_name.setdefault(os.path.basename(path), set()).add(mod)
    return by_name


def owner_of(obj, by_name):
    m = MEMBER.match(obj.strip())
    if m:
        lib, member = m.group(2), m.group(3)
        if lib.startswith('libokay_engine'):
            src = member[:-2] if member.endswith('.o') else member[:-4] if member.endswith('.obj') else member
            mods = by_name.get(src)
            if not mods:
                return 'core'          # an engine object no manifest claims, or one made at build time
            return next(iter(mods)) if len(mods) == 1 else '(same file name)'
        return 'library: ' + lib
    if '/' in obj or '\\' in obj or obj.endswith(('.o', '.obj')):
        if os.path.basename(obj).startswith(('crt', 'libc', 'ld-')):
            return 'library: ' + os.path.basename(obj)
        return 'program'
    return 'library: ' + obj


def sizes(map_text, by_name):
    out, bss = {}, {}
    started = False
    pending = None
    for raw in map_text.splitlines():
        if not started:
            started = raw.startswith('Linker script and memory map')
            continue
        m = NAME_ONLY.match(raw)
        if m:
            pending = m.group(1)
            continue
        m = ROW.match(raw)
        if not m:
            pending = None
            continue
        name = m.group(1) or pending
        pending = None
        if not name or not raw.startswith(' ') or name.startswith(SKIP):
            continue
        size = int(m.group(3), 16)
        if size == 0:
            continue
        who = owner_of(m.group(4), by_name)
        target = bss if name.startswith(('.bss', '.tbss')) else out
        target[who] = target.get(who, 0) + size
    return out, bss


def main():
    args = [a for a in sys.argv[1:]]
    json_out = None
    if '--json' in args:
        i = args.index('--json')
        json_out = args[i + 1]
        del args[i:i + 2]
    if len(args) != 2:
        print(__doc__)
        return 2
    by_name = read_owners(args[0])
    with open(args[1], encoding='utf-8', errors='replace') as f:
        file_bytes, bss = sizes(f.read(), by_name)
    total = sum(file_bytes.values()) or 1
    rows = sorted(set(file_bytes) | set(bss), key=lambda k: -file_bytes.get(k, 0))
    print('| Part | Bytes in the file | % | .bss |')
    print('|---|---|---|---|')
    for k in rows:
        print(f'| {k} | {file_bytes.get(k, 0)} | {100.0 * file_bytes.get(k, 0) / total:.1f}% | {bss.get(k, 0)} |')
    print(f'| **total** | {sum(file_bytes.values())} | | {sum(bss.values())} |')
    if json_out:
        with open(json_out, 'w', encoding='utf-8') as f:
            json.dump({'map': os.path.basename(args[1]), 'bytes': file_bytes, 'bss': bss,
                       'total': sum(file_bytes.values())}, f, indent=1, sort_keys=True)
            f.write('\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
