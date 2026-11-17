#!/usr/bin/env python3
"""Module lists of the editor's presets, with requirements added, for the preset player builds.
  preset_modules.py 2d            comma list for -DOKAY_MODULES=
  preset_modules.py --all         one line per preset: "<id> <list>"
  preset_modules.py --smallest a,b,c   the smallest preset whose list contains every module given
Reads the presets from editor/ModulesModel.hpp (one copy of the data) and requirements from engine/modules."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_modules as cm

def presets():
    t = open(os.path.join(cm.ROOT, 'editor', 'ModulesModel.hpp'), encoding='utf-8').read()
    out = {}
    for m in re.finditer(r'\{"(\w+)",\s*"[^"]*",\s*"[^"]*",\s*\{([^}]*)\}\}', t):
        out[m.group(1)] = re.findall(r'"(\w+)"', m.group(2))
    return out

def close(mods, wanted):
    on = {n for n, m in mods.items() if m.get('core') == '1'}
    stack = list(wanted)
    while stack:
        n = stack.pop()
        if n in on or n not in mods: continue
        on.add(n); stack += mods[n]['requires']
    return on

def main():
    mods = cm.read_manifests()
    pre = presets()
    sets = {k: close(mods, v if v else list(mods)) for k, v in pre.items()}   # an empty list = every module
    a = sys.argv[1:]
    if a[:1] == ['--all']:
        for k, s in sets.items(): print(k, ','.join(sorted(s)))
    elif a[:1] == ['--smallest']:
        need = close(mods, [x for x in a[1].split(',') if x])
        fit = [(len(s), k) for k, s in sets.items() if need <= s]
        print(min(fit)[1] if fit else 'full')
    else:
        print(','.join(sorted(sets[a[0]])))

main()
