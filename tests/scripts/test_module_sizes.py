#!/usr/bin/env python3
"""scripts/module_sizes.py on a made-up GNU ld map and okay_modules.tsv. Usage: test_module_sizes.py <module_sizes.py>"""
import json, os, subprocess, sys, tempfile

SCRIPT = os.path.abspath(sys.argv[1])
MAP = """Archive member included to satisfy reference by file (symbol)

Discarded input sections

 .text          0x0000000000000000      0x500 engine/libokay_engine.a(Physics3D.cpp.o)

Memory Configuration

Linker script and memory map

.text           0x0000000000401000     0x2000
 *(.text .text.*)
 .text          0x0000000000401000      0x100 CMakeFiles/okay-player.dir/main.cpp.o
                0x0000000000401000                main
 .text          0x0000000000401100      0x200 ../engine/libokay_engine.a(Physics3D.cpp.o)
 .text._ZN4okay15AVeryLongFunctionNameThatWrapsEv
                0x0000000000401300       0x40 ../engine/libokay_engine.a(Physics3D.cpp.o)
 *fill*         0x0000000000401340        0x0
 .text          0x0000000000401340       0x80 ../engine/libokay_engine.a(Damage.cpp.o)
 .text          0x00000000004013c0       0x10 ../engine/libokay_engine.a(Same.cpp.o)
 .text          0x00000000004013d0       0x20 ../engine/libokay_engine.a(Scene.cpp.o)
 .text          0x00000000004013f0       0x30 /usr/lib/x86_64-linux-gnu/libSDL2.a(SDL.o)
.rodata         0x0000000000403000      0x100
 .rodata        0x0000000000403000        0x8 ../engine/libokay_engine.a(Damage.cpp.o)
.bss            0x0000000000404000      0x100
 .bss           0x0000000000404000       0x20 ../engine/libokay_engine.a(Physics3D.cpp.o)
.debug_info     0x0000000000000000     0x9999
 .debug_info    0x0000000000000000     0x9999 ../engine/libokay_engine.a(Physics3D.cpp.o)
"""
TSV = "physics3d\tsrc/Physics/Physics3D.cpp\nhealth_damage\tsrc/Components/Damage.cpp\nai_navigation\tsrc/AI/Same.cpp\n" \
      "ui_kit\tsrc/UI/Same.cpp\ncore\tsrc/Scene/Scene.cpp\n"
fails = 0
with tempfile.TemporaryDirectory() as d:
    open(os.path.join(d, 'okay_modules.tsv'), 'w').write(TSV)
    open(os.path.join(d, 'p.map'), 'w').write(MAP)
    out = os.path.join(d, 'sizes.json')
    p = subprocess.run([sys.executable, '-I', SCRIPT, d, os.path.join(d, 'p.map'), '--json', out],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if p.returncode != 0:
        print(p.stdout); sys.exit(1)
    got = json.load(open(out))
    want = {'physics3d': 0x240, 'health_damage': 0x88, '(same file name)': 0x10, 'core': 0x20, 'program': 0x100,
            'library: libSDL2.a': 0x30}
    for k, v in want.items():
        if got['bytes'].get(k) != v:
            fails += 1
            print('FAIL: %s is %r, want %r' % (k, got['bytes'].get(k), v))
    if got['bss'].get('physics3d') != 0x20:
        fails += 1; print('FAIL: bss %r' % got['bss'])
    if got['total'] != sum(want.values()):
        fails += 1; print('FAIL: total %r' % got['total'])
    if '| physics3d | 576 |' not in p.stdout:
        fails += 1; print('FAIL: table\n' + p.stdout)
print('module_sizes fixture: ' + ('OK' if not fails else '%d failure(s)' % fails))
sys.exit(1 if fails else 0)
