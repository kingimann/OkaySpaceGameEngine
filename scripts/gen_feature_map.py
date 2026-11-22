#!/usr/bin/env python3
"""Builds docs/FEATURE_MAP.md from the detail pages in docs/feature_map/*.md.

The pages are the source of truth. Each page holds one or more areas ("## N. Name") and each area
holds sub-areas ("### N.M Title") written as labelled lines ("- **What:** ..."). This script makes the
areas table, the per-area summary tables, the "By what you see" table and the A-Z index, and puts the
hand-written text of docs/feature_map/_header.md and _footer.md around them. The two big lookup
tables are written to docs/feature_map/_index_symptoms.md and _index_az.md so every file stays
small enough to render on GitHub. It also writes docs/SYSTEMS.md, the list of named systems, from the
"- **System:**" lines of the sub-areas (one row per system: name, what it does, status, main code,
sub-areas, plus the duplicates found).

  python3 scripts/gen_feature_map.py            rewrite docs/FEATURE_MAP.md
  python3 scripts/gen_feature_map.py --check    exit 1 if docs/FEATURE_MAP.md is out of date
  python3 scripts/gen_feature_map.py --strict   also exit 1 on format warnings
Page format: docs/feature_map/README.md
"""
import argparse, glob, os, re, sys

FIELD_RE = re.compile(r'^- \*\*([^:*]+):\*\*\s*(.*)$')
H1_RE = re.compile(r'^# ')
H2_RE = re.compile(r'^## (\d+)\. (.+?)\s*$')
H3_RE = re.compile(r'^### (\d+\.\d+) (.+?)\s*$')
PART_RE = re.compile(r'^\s+- (\d+\.\d+\.\d+) (.+?)\s*$')
REQUIRED = ('What', 'Code', 'Status', 'Words')
STATUS_WORDS = ('In flight', 'Works', 'Partial', 'Basic', 'Planned', 'Removed')
DUP_PHRASES = ('being merged into', 'duplicate of')


def slug(text):
    """GitHub heading anchor."""
    s = text.strip().lower()
    s = re.sub(r'[^a-z0-9 _-]', '', s)
    return s.replace(' ', '-')


def parse_page(path, problems):
    rel = os.path.basename(path)
    h1, areas = [], []
    area = sub = field = None
    mode = None
    with open(path, encoding='utf-8') as f:
        lines = f.read().split('\n')
    for n, line in enumerate(lines, 1):
        if H1_RE.match(line) and not line.startswith('## '):
            mode = 'h1'; continue
        m = H2_RE.match(line)
        if m:
            area = {'num': int(m.group(1)), 'name': m.group(2), 'page': rel, 'intro': [], 'subs': []}
            areas.append(area); sub = field = None; mode = 'intro'; continue
        m = H3_RE.match(line)
        if m:
            if area is None:
                problems.append(f'{rel}:{n}: sub-area before any "## N. Name" heading'); continue
            sub = {'id': m.group(1), 'title': m.group(2), 'f': {}, 'lists': {}, 'order': [], 'page': rel, 'line': n}
            if not sub['id'].startswith(f"{area['num']}."):
                problems.append(f"{rel}:{n}: sub-area {sub['id']} is not inside area {area['num']}")
            area['subs'].append(sub); field = None; mode = 'sub'; continue
        if mode == 'h1':
            h1.append(line)
        elif mode == 'intro' and area is not None:
            area['intro'].append(line)
        elif mode == 'sub' and sub is not None:
            fm = FIELD_RE.match(line)
            if fm:
                field = fm.group(1).strip()
                sub['f'][field] = fm.group(2).strip(); sub['order'].append(field)
            elif field and line.strip():
                st = line.strip()
                if st.startswith('- ') and line.startswith((' ', '\t')):
                    sub['lists'].setdefault(field, []).append(st[2:].strip())
                elif line.startswith((' ', '\t')):
                    sub['f'][field] = (sub['f'][field] + ' ' + st).strip()
    for a in areas:
        a['intro'] = '\n'.join(a['intro']).strip() or '\n'.join(h1).strip()
        for s in a['subs']:
            for need in REQUIRED:
                if not s['f'].get(need):
                    problems.append(f"{rel}:{s['line']}: {s['id']} is missing '{need}'")
            for p in s['lists'].get('Parts', []):
                pm = re.match(r'(\d+\.\d+\.\d+) (.+)$', p)
                if not pm:
                    problems.append(f"{rel}:{s['line']}: {s['id']} Parts line does not start with an ID: {p[:50]}")
                elif not pm.group(1).startswith(s['id'] + '.'):
                    problems.append(f"{rel}:{s['line']}: part {pm.group(1)} is not inside {s['id']}")
    return areas


def short(text, limit):
    text = re.sub(r'\s+', ' ', text.strip())
    if len(text) <= limit:
        return text
    cut = text[:limit].rsplit(' ', 1)[0]
    return cut.rstrip(',;:-') + '...'


def esc(text):
    return text.replace('|', '\\|')


def split_terms(value, sep):
    out = []
    for t in value.split(sep):
        t = t.strip().strip('`').strip()
        t = t.strip('"').strip("'").strip()
        if t and t != '-':
            out.append(t)
    return out


def idkey(i):
    return tuple(int(x) for x in i.split('.'))


def status_word(text):
    """The leading status word of a Status line ("Works for the cursor..." -> "Works")."""
    t = text.strip()
    for w in STATUS_WORDS:
        if t.lower().startswith(w.lower()):
            return w
    return short(t.split(' - ')[0], 30)


def system_entries(sub):
    """The "- **System:**" values of a sub-area: one inline value and/or a nested list."""
    vals = list(sub['lists'].get('System', []))
    if sub['f'].get('System'):
        vals.insert(0, sub['f']['System'])
    return vals


def resolve_code(root, token):
    """Repo paths a Code token names (engine headers are written without engine/include/okay/ and .hpp)."""
    token = token.strip()
    if not token or ' ' in token or token.startswith(('-', '#')):
        return []
    for base in ('', 'engine/include/okay/'):
        if any(c in token for c in '*?['):
            hits = sorted(os.path.relpath(h, root).replace(os.sep, '/')
                          for h in glob.glob(os.path.join(root, base + token)))
            if hits:
                return hits
    for c in (token, f'engine/include/okay/{token}.hpp', f'engine/include/okay/{token}.h',
              f'engine/include/okay/{token}', f'engine/src/{token}.cpp', f'engine/src/{token}'):
        if os.path.exists(os.path.join(root, c)):
            return [c.rstrip('/')]
    return []


def main_code(root, paths, limit=5):
    """Files, or folders when a folder holds three or more of them, biggest first; every one exists."""
    if len(paths) <= limit:
        cells = [f"`{p}/`" if os.path.isdir(os.path.join(root, p)) else f"`{p}`" for p in paths]
    else:
        groups = {}
        for p in paths:
            d = p if os.path.isdir(os.path.join(root, p)) else os.path.dirname(p)
            groups.setdefault(d, set()).add(p)
        cells = []
        for d, files in sorted(groups.items(), key=lambda kv: (-len(kv[1]), kv[0])):
            if d and (len(files) >= 3 or d in files):
                cells.append(f"`{d}/`")
            else:
                cells += [f"`{f}`" for f in sorted(files)]
    more = len(cells) - limit
    return ', '.join(cells[:limit]) + (f" (+{more} more)" if more > 0 else '') if cells else '-'


def build_systems(root, areas, problems, link):
    """docs/SYSTEMS.md from the System lines. Returns (text, number of systems)."""
    systems, order = {}, []
    for a in areas:
        for s in a['subs']:
            vals = system_entries(s)
            if not vals:
                problems.append(f"{s['page']}:{s['line']}: {s['id']} has no 'System' line")
            for v in vals:
                code = None
                if ' Code: ' in v:  # this system's own files when a sub-area holds several systems
                    v, _, code = v.rpartition(' Code: ')
                name, _, desc = v.partition(' - ')
                name = name.strip()
                e = systems.get(name)
                if e is None:
                    e = systems[name] = {'name': name, 'desc': '', 'home': None, 'subs': [], 'code': {}}
                    order.append(name)
                if code is not None:
                    e['code'][s['id']] = code
                elif len(vals) > 1:
                    problems.append(f"{s['page']}:{s['line']}: {s['id']} holds several systems; give '{name}' "
                                    "its own ' Code: `...`' list")
                if desc.strip():
                    if e['home'] is not None:
                        problems.append(f"{s['page']}:{s['line']}: system '{name}' is described twice "
                                        f"({e['home'][1]['id']} and {s['id']})")
                    else:
                        e['desc'], e['home'] = desc.strip(), (a, s)
                e['subs'].append((a, s))
    for name in order:
        e = systems[name]
        if e['home'] is None:
            a, s = e['subs'][0]
            problems.append(f"{s['page']}:{s['line']}: system '{name}' has no ' - what it does' line anywhere")
            e['home'], e['desc'] = (a, s), ''
        e['dup'] = None
        for ph in DUP_PHRASES:
            i = e['desc'].find(' - ' + ph + ' ')
            if i >= 0:
                rest = e['desc'][i + len(ph) + 4:]
                target = next((n for n in sorted(systems, key=len, reverse=True) if rest.startswith(n)), None)
                if target is None or target == name:
                    problems.append(f"{e['home'][1]['page']}:{e['home'][1]['line']}: system '{name}' says "
                                    f"'{ph}' but names no other system")
                else:
                    e['dup'] = (ph, target, rest[len(target):].strip())
                    e['desc'] = e['desc'][:i].strip()
                break
    lines = ['# Systems', '',
             'Generated from the `System:` lines of the feature map pages by `scripts/gen_feature_map.py`; do not edit.',
             'Back to the [feature map](FEATURE_MAP.md).', '',
             'A **system** is a named part of OkaySpace that does one job from start to end (Physics, Save Games, '
             'OkayScript, the Launcher Updater). Each system has one official name and is made of one or more '
             'sub-areas of the feature map. Its home area is the area of the sub-area that describes it.', '',
             '- **Status** comes from the sub-areas\' Status lines (Works, Partial, Basic, Planned, In flight); '
             'a system made of sub-areas in different states lists how many are in each.',
             '- **Main code** lists its files, or the folders that hold three or more of them, largest first. '
             'Engine headers are under `engine/include/okay/`, their code under `engine/src/`.',
             '- **Feature map** links every sub-area that is part of it; the first one is the one that describes it.',
             '- To name or change a system, edit the `- **System:**` line of a sub-area in `docs/feature_map/` '
             '(format in `docs/feature_map/README.md`), then run `python3 scripts/gen_feature_map.py`.', '']
    byarea = {}
    for name in order:
        byarea.setdefault(systems[name]['home'][0]['num'], []).append(systems[name])
    lines += ['## Areas', '']
    for a in areas:
        if a['num'] in byarea:
            lines.append(f"- [{a['num']}. {a['name']}](#{slug(str(a['num']) + ' ' + a['name'])}): "
                         + ', '.join(e['name'] for e in sorted(byarea[a['num']], key=lambda e: idkey(e['home'][1]['id']))))
    lines += ['- [Duplicates found](#duplicates-found)', '']
    for a in areas:
        if a['num'] not in byarea:
            continue
        lines += [f"## {a['num']}. {a['name']}", '',
                  '| System | What it does | Status | Main code | Feature map |', '|---|---|---|---|---|']
        for e in sorted(byarea[a['num']], key=lambda e: idkey(e['home'][1]['id'])):
            home = e['home'][1]
            subs = [home] + sorted((s for _, s in e['subs'] if s is not home), key=lambda s: idkey(s['id']))
            counts = {}
            for s in subs:
                w = status_word(s['f'].get('Status', ''))
                counts[w] = counts.get(w, 0) + 1
            if len(counts) == 1:
                status = next(iter(counts))
            else:
                rank = {w: i for i, w in enumerate(STATUS_WORDS)}
                status = ', '.join(f"{n} {w}" for w, n in sorted(counts.items(), key=lambda kv: (-kv[1], rank.get(kv[0], 99), kv[0])))
            if e['dup']:
                status += ' (duplicate, see [below](#duplicates-found))'
            paths = []
            for s in subs:
                for tok in re.findall(r'`([^`]+)`', e['code'].get(s['id'], s['f'].get('Code', ''))):
                    for p in resolve_code(root, tok):
                        if p not in paths:
                            paths.append(p)
            pages = {s['id']: s['page'] for _, s in e['subs']}
            links = ', '.join(link(s['id'], pages[s['id']], s['title']) for s in subs)
            lines.append(f"| **{esc(e['name'])}** | {esc(e['desc'])} | {esc(status)} | {esc(main_code(root, paths))} | {links} |")
        lines.append('')
    lines += ['## Duplicates found', '',
              'Systems that do the same job as another system. "Being merged" means a pull request is folding it into '
              'the other one; any other row means both stay for now, for the reason given. Found while naming the '
              'systems and from the duplicate audits on issue #422; the code is not changed here.', '',
              '| System | Same job as | State | Feature map |', '|---|---|---|---|']
    dups = [systems[n] for n in order if systems[n]['dup']]
    for e in sorted(dups, key=lambda e: idkey(e['home'][1]['id'])):
        ph, target, note = e['dup']
        note = note.strip()
        if note.startswith('(') and note.endswith(')'):
            note = note[1:-1]
        home = e['home'][1]
        state = f"Being merged ({note})" if ph == 'being merged into' else (note[:1].upper() + note[1:] or 'Kept for now')
        lines.append(f"| **{esc(e['name'])}** | {esc(target)} | {esc(state)} | "
                     f"{link(home['id'], home['page'], home['title'])} |")
    if not dups:
        lines.append('| - | - | none recorded | - |')
    lines.append('')
    return '\n'.join(lines), systems


def build(root):
    problems = []
    pages = sorted(p for p in glob.glob(os.path.join(root, 'docs', 'feature_map', '*.md'))
                   if not os.path.basename(p).startswith('_') and os.path.basename(p) != 'README.md')
    areas = []
    for p in pages:
        areas += parse_page(p, problems)
    byn = {}
    for a in areas:
        if a['num'] in byn:
            problems.append(f"area {a['num']} is defined in both {byn[a['num']]['page']} and {a['page']}")
        byn[a['num']] = a
    areas = [byn[k] for k in sorted(byn)]
    ids = {}
    for a in areas:
        for s in a['subs']:
            if s['id'] in ids:
                problems.append(f"sub-area {s['id']} appears twice ({ids[s['id']]} and {s['page']})")
            ids[s['id']] = s['page']
    nsub = sum(len(a['subs']) for a in areas)
    nparts = sum(len(s['lists'].get('Parts', [])) for a in areas for s in a['subs'])

    def link2(sid, page, title):
        return f"[{sid}]({page}#{slug(sid + ' ' + title)})"

    def link(sid, page, title):
        return f"[{sid}](feature_map/{page}#{slug(sid + ' ' + title)})"

    hdr = open(os.path.join(root, 'docs', 'feature_map', '_header.md'), encoding='utf-8').read().rstrip('\n')
    ftr = open(os.path.join(root, 'docs', 'feature_map', '_footer.md'), encoding='utf-8').read().strip('\n')
    # No counts here: a count changes with every pull request and made this file conflict on every merge.
    hdr = hdr.replace('{{STATS}}', 'every area, sub-area and numbered part')
    out = [hdr, '', '## Areas', '', '| # | Area | Sub-areas |', '|---|---|---|']
    for a in areas:
        subs = ', '.join(f"[{s['id']}](feature_map/{a['page']}#{slug(s['id'] + ' ' + s['title'])}) {esc(s['title'])}"
                         for s in a['subs'])
        out.append(f"| {a['num']} | {esc(a['name'])} | {subs} |")
    out.append('')
    for a in areas:
        out += [f"## {a['num']}. {a['name']}", '']
        if a['intro']:
            out += [a['intro'], '']
        out += [f"Details for every sub-area and numbered part: [docs/feature_map/{a['page']}](feature_map/{a['page']}).", '']
        out += ['| ID | Sub-area | Status | What | Open |', '|---|---|---|---|---|']
        for s in a['subs']:
            f = s['f']
            status = f.get('Status', '').split(' - ')[0].strip()
            out.append(f"| {link(s['id'], a['page'], s['title'])} | {esc(s['title'])} | {esc(status)} | "
                       f"{esc(short(f.get('What', ''), 150))} | {esc(short(f.get('Open', '-'), 130))} |")
        out.append('')
    # By what you see (own page)
    rows = []
    for a in areas:
        for s in a['subs']:
            for ph in split_terms(s['f'].get('Seen as', ''), ';'):
                rows.append((ph, s, a))
    rows.sort(key=lambda r: (r[0].lower(), idkey(r[1]['id'])))
    sym = ['# By what you see', '',
           'Generated from the `Seen as` lines of the detail pages by `scripts/gen_feature_map.py`; do not edit.',
           'Find the window, page or symptom, then say the number. Back to the [overview](../FEATURE_MAP.md).', '',
           '| You see or say | Sub-area |', '|---|---|']
    for ph, s, a in rows:
        sym.append(f"| {esc(ph)} | {link2(s['id'], a['page'], s['title'])} {esc(s['title'])} |")
    sym.append('')
    # A-Z index (own page)
    idx = {}
    def add(term, sid):
        k = term.lower()
        e = idx.setdefault(k, [term, set()])
        e[1].add(sid)
    for a in areas:
        add(a['name'].split(' (')[0], str(a['num']))
        for s in a['subs']:
            add(s['title'], s['id'])
            for t_ in split_terms(s['f'].get('Words', ''), ','):
                add(t_, s['id'])
            for p_ in s['lists'].get('Parts', []):
                pm = re.match(r'(\d+\.\d+\.\d+) (.+?)(?: - |$)', p_)
                if pm:
                    add(pm.group(2), pm.group(1))
    for a in areas:  # system names (docs/SYSTEMS.md) lead to every sub-area of the system
        for s in a['subs']:
            for v in system_entries(s):
                add(v.split(' Code: ')[0].split(' - ')[0].strip(), s['id'])
    az = ['# A-Z index', '',
          'Generated from the `Words` lines and titles of the detail pages by `scripts/gen_feature_map.py`; do not edit.',
          'Word, then the number(s) to say. Back to the [overview](../FEATURE_MAP.md).', '']
    letters = {}
    for k in sorted(idx):
        term, sids = idx[k]
        letter = k[0].upper() if k[0].isalpha() else '#'
        letters.setdefault(letter, []).append(
            f"{term} ({', '.join(sorted(sids, key=lambda x: tuple(int(y) for y in x.split('.'))))})")
    for letter in sorted(letters, key=lambda c: (c == '#', c)):
        # One entry per line so two pull requests that add different words merge cleanly.
        az.append(f"**{letter}**")
        az.append('')
        az += [f"- {e}" for e in letters[letter]]
        az.append('')
    out += ['---', '', ftr, '']
    systems_md, systems = build_systems(root, areas, problems, link)
    return {'docs/FEATURE_MAP.md': '\n'.join(out),
            'docs/SYSTEMS.md': systems_md,
            'docs/feature_map/_index_symptoms.md': '\n'.join(sym),
            'docs/feature_map/_index_az.md': '\n'.join(az)}, problems, (len(areas), nsub, nparts, len(systems))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--strict', action='store_true')
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    outputs, problems, (na, ns, npart, nsys) = build(root)
    for p in problems:
        print('warning:', p, file=sys.stderr)
    stale = []
    for rel, text in outputs.items():
        target = os.path.join(root, rel)
        cur = open(target, encoding='utf-8').read() if os.path.exists(target) else None
        if cur != text:
            stale.append(rel)
            if not a.check:
                with open(target, 'w', encoding='utf-8', newline='\n') as f:
                    f.write(text)
    if a.check:
        if stale:
            print('out of date: ' + ', '.join(stale) + ': run python3 scripts/gen_feature_map.py', file=sys.stderr)
            return 1
        print(f'up to date ({na} areas, {ns} sub-areas, {npart} parts, {nsys} systems)')
        return 1 if (a.strict and problems) else 0
    print(f'wrote {len(outputs)} files ({na} areas, {ns} sub-areas, {npart} parts, {nsys} systems, {len(problems)} warnings)')
    return 1 if (a.strict and problems) else 0


if __name__ == '__main__':
    sys.exit(main())
