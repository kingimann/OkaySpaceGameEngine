#!/usr/bin/env python3
"""Checks the engine's module manifests (engine/modules/*/module.okaymodule) against the code.

  scripts/check_modules.py             report; exit 1 on any NEW forbidden include or broken manifest
  scripts/check_modules.py --root DIR  check another tree (the fixture test uses it)
  scripts/check_modules.py --all       also list the known violations in the baseline
  scripts/check_modules.py --matrix    print a module-to-module include table (markdown)
  scripts/check_modules.py --update-baseline   rewrite scripts/module_deps_baseline.txt (use only when
                                       a violation was removed; never to hide a new one)

Stage 0 (design PR 2): CI runs it in report mode (a failure is a warning, see .github/workflows/branch-lint.yml); it
becomes blocking in design PR 14.

Rules (docs/engine/modular_engine.md, "The dependency check"):
  1. A file owned by module A may include a header owned by B only if B is A, B is `core`, or B is in A's
     transitive `requires`. Anything else is a forbidden include.
  2. `core` includes no other module (core has no requires, so rule 1 covers it).
  3. The `requires` graph has no cycles and names only existing modules.
  4. Every manifest source pattern matches a file; every listed component class and scene record exists in
     the code; every module name is a legal id.
  5. `Okay.hpp` (the umbrella) is not included from engine/.

Today's code has many violations (the central serializer, script VM and renderer name every component).
They are listed in scripts/module_deps_baseline.txt, one "file -> module" line each. The check fails when a
file gains a violation that is not in the baseline, so the list can only shrink. Stdlib only.
"""
import fnmatch, os, re, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
if '--root' in sys.argv[1:-1]:
    ROOT = os.path.abspath(sys.argv[sys.argv.index('--root') + 1])
ENG = os.path.join(ROOT, 'engine')
BASELINE = os.path.join(ROOT, 'scripts', 'module_deps_baseline.txt')
INC = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


def read_manifests():
    mods = {}
    base = os.path.join(ENG, 'modules')
    for d in sorted(os.listdir(base)) if os.path.isdir(base) else []:
        p = os.path.join(base, d, 'module.okaymodule')
        if not os.path.isfile(p):
            continue
        m = {'dir': d, 'path': p}
        for line in open(p, encoding='utf-8'):
            line = line.strip()
            if not line or line.startswith('#') or '=' not in line:
                continue
            k, v = line.split('=', 1)
            m[k.strip()] = v.strip()
        for k in ('requires', 'provides', 'components', 'serializers', 'editor', 'vs_nodes'):
            m[k] = [x.strip() for x in m.get(k, '').split(',') if x.strip()]
        m['sources'] = m.get('sources', '').split()
        mods[m.get('name', d)] = m
    return mods


def source_files():
    out = []
    for root in ('include', 'src', 'modules'):
        for dp, _, fs in os.walk(os.path.join(ENG, root)):
            for f in fs:
                if f.endswith(('.hpp', '.h', '.cpp', '.inl', '.inc')):
                    out.append(os.path.relpath(os.path.join(dp, f), ENG).replace(os.sep, '/'))
    return sorted(out)


def literal_len(pat):
    m = re.search(r'[*?\[]', pat)
    return len(pat) if not m else m.start()


def build_owner(mods, files):
    pats = []
    for n, m in mods.items():
        for p in m['sources']:
            pats.append((p, n))
    owner = {}
    for f in files:
        if f.startswith('modules/'):          # engine/modules/<name>/register.cpp etc. belong to <name>
            owner[f] = f.split('/')[1] if f.split('/')[1] in mods else 'core'
            continue
        best, bl = 'core', -1
        for p, n in pats:
            if fnmatch.fnmatch(f, p) and literal_len(p) > bl:
                best, bl = n, literal_len(p)
        owner[f] = best
    return owner


def closure(mods):
    memo = {}
    def go(n, stack):
        if n in memo:
            return memo[n]
        out = set()
        for r in mods.get(n, {}).get('requires', []):
            if r in mods and r not in stack:
                out.add(r)
                out |= go(r, stack | {n})
        memo[n] = out
        return out
    return {n: go(n, frozenset()) for n in mods}


def resolve_include(frm, inc, fileset):
    if inc.startswith('okay/'):
        c = 'include/' + inc
        return c if c in fileset else None
    c = os.path.normpath(os.path.join(os.path.dirname(frm), inc)).replace(os.sep, '/')
    return c if c in fileset else None


def main():
    args = sys.argv[1:]
    mods = read_manifests()
    files = source_files()
    fileset = set(files)
    owner = build_owner(mods, files)
    clo = closure(mods)
    problems = []

    # rule 3, 4 (manifest sanity)
    for n, m in mods.items():
        if not re.fullmatch(r'[a-z0-9_]+', n):
            problems.append(f"{m['path']}: bad module name '{n}'")
        for r in m['requires']:
            if r not in mods:
                problems.append(f"module '{n}' requires unknown module '{r}'")
        if n in clo.get(n, set()):
            problems.append(f"module '{n}' is part of a requires cycle")
        for r in m['requires']:
            if n in clo.get(r, set()):
                problems.append(f"modules '{n}' and '{r}' require each other")
        for p in m['sources']:
            if not any(fnmatch.fnmatch(f, p) for f in files):
                problems.append(f"module '{n}': source pattern '{p}' matches no file")
    allheaders = '\n'.join(open(os.path.join(ENG, f), encoding='utf-8', errors='replace').read()
                           for f in files if f.startswith('include/okay/Components/') or f.startswith('include/okay/Modules/'))
    ser = ''
    for f in files:
        if f.startswith(('src/Scene/', 'src/Components/', 'modules/')):
            ser += open(os.path.join(ENG, f), encoding='utf-8', errors='replace').read()
    seen_c, seen_r = {}, {}
    for n, m in mods.items():
        for c in m['components']:
            if not re.search(r'\b(class|struct)\s+' + re.escape(c) + r'\b', allheaders):
                problems.append(f"module '{n}': component '{c}' is not declared in Components/ or Modules/")
            if c in seen_c and seen_c[c] != n:
                problems.append(f"component '{c}' is listed by '{seen_c[c]}' and '{n}'")
            seen_c[c] = n
        for r in m['serializers']:
            if '"' + r + '"' not in ser:
                problems.append(f"module '{n}': scene record '{r}' not found in the serializer code")
            if r in seen_r and seen_r[r] != n:
                problems.append(f"record '{r}' is listed by '{seen_r[r]}' and '{n}'")
            seen_r[r] = n

    # rule 1, 2, 5 (includes)
    viol = {}      # (file, target module) -> [included files]
    matrix = {}
    for f in files:
        a = owner[f]
        try:
            text = open(os.path.join(ENG, f), encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        for inc in INC.findall(text):
            if inc == 'Okay.hpp' or inc.endswith('/Okay.hpp'):
                problems.append(f"{f}: includes the umbrella Okay.hpp (engine code must include what it uses)")
                continue
            t = resolve_include(f, inc, fileset)
            if not t:
                continue
            b = owner[t]
            if a == b:
                continue
            matrix[(a, b)] = matrix.get((a, b), 0) + 1
            if b == 'core' or b in clo.get(a, set()):
                continue
            viol.setdefault((f, b), []).append(t)

    if '--matrix' in args:
        names = sorted({a for a, _ in matrix} | {b for _, b in matrix})
        print('| from \\ includes | ' + ' | '.join(names) + ' |')
        print('|---|' + '---|' * len(names))
        for a in names:
            row = [str(matrix.get((a, b), '')) for b in names]
            print(f'| {a} | ' + ' | '.join(row) + ' |')
        return 0

    keys = sorted(f'{f} -> {b}' for (f, b) in viol)
    if '--update-baseline' in args:
        with open(BASELINE, 'w', encoding='utf-8') as fh:
            fh.write('# Known forbidden includes (file -> module it includes without requiring it).\n'
                     '# scripts/check_modules.py fails on any line that is not here. Only ever shrink this file.\n')
            fh.write('\n'.join(keys) + '\n')
        print(f'baseline written: {len(keys)} entries')
        return 0
    base = set()
    if os.path.isfile(BASELINE):
        base = {l.strip() for l in open(BASELINE, encoding='utf-8') if l.strip() and not l.startswith('#')}
    new = [k for k in keys if k not in base]
    fixed = sorted(base - set(keys))
    for p in problems:
        print('ERROR: ' + p)
    for k in new:
        f, b = k.split(' -> ')
        print(f"ERROR: module {owner[f]} ({f}) includes {viol[(f, b)][0]} (module {b}) but {b} is not in {owner[f]}.requires")
    if '--all' in args:
        for k in keys:
            if k in base:
                print('known: ' + k)
    if fixed:
        print(f'note: {len(fixed)} baseline entries no longer apply; run --update-baseline to drop them')
    print(f'modules: {len(mods)}, files: {len(files)}, known violations: {len(keys) - len(new)}, '
          f'new violations: {len(new)}, manifest problems: {len(problems)}')
    return 1 if (new or problems) else 0


if __name__ == '__main__':
    sys.exit(main())
