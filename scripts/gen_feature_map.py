#!/usr/bin/env python3
"""Builds docs/FEATURE_MAP.md from the detail pages in docs/feature_map/*.md.

The pages are the source of truth. Each page holds one or more areas ("## N. Name") and each area
holds sub-areas ("### N.M Title") written as labelled lines ("- **What:** ..."). This script makes the
areas table, the per-area summary tables, the "By what you see" table and the A-Z index, and puts the
hand-written text of docs/feature_map/_header.md and _footer.md around them.

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

    def link(sid, page, title):
        return f"[{sid}](feature_map/{page}#{slug(sid + ' ' + title)})"

    hdr = open(os.path.join(root, 'docs', 'feature_map', '_header.md'), encoding='utf-8').read().rstrip('\n')
    ftr = open(os.path.join(root, 'docs', 'feature_map', '_footer.md'), encoding='utf-8').read().strip('\n')
    hdr = hdr.replace('{{STATS}}', f'{len(areas)} areas, {nsub} sub-areas and {nparts} numbered parts')
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
    # By what you see
    rows = []
    for a in areas:
        for s in a['subs']:
            for ph in split_terms(s['f'].get('Seen as', ''), ';'):
                rows.append((ph, s, a))
    rows.sort(key=lambda r: (r[0].lower(), idkey(r[1]['id'])))
    out += ['## By what you see', '', 'Find the window, page or symptom, then say the number.', '',
            '| You see or say | Sub-area |', '|---|---|']
    for ph, s, a in rows:
        out.append(f"| {esc(ph)} | {link(s['id'], a['page'], s['title'])} {esc(s['title'])} |")
    out.append('')
    # A-Z index
    idx = {}
    def add(term, sid):
        k = term.lower()
        e = idx.setdefault(k, [term, set()])
        e[1].add(sid)
    for a in areas:
        add(a['name'].split(' (')[0], str(a['num']))
        for s in a['subs']:
            add(s['title'], s['id'])
            for t in split_terms(s['f'].get('Words', ''), ','):
                add(t, s['id'])
            for p in s['lists'].get('Parts', []):
                pm = re.match(r'(\d+\.\d+\.\d+) (.+?)(?: - |$)', p)
                if pm:
                    add(pm.group(2), pm.group(1))
    out += ['## A-Z index', '', 'Word, then the number(s) to say.', '']
    letters = {}
    for k in sorted(idx):
        term, sids = idx[k]
        letter = k[0].upper() if k[0].isalpha() else '#'
        letters.setdefault(letter, []).append(
            f"{term} ({', '.join(sorted(sids, key=lambda x: tuple(int(y) for y in x.split('.'))))})")
    for letter in sorted(letters, key=lambda c: (c == '#', c)):
        out.append(f"**{letter}** " + ' · '.join(letters[letter]))
        out.append('')
    out += ['---', '', ftr, '']
    return '\n'.join(out), problems, (len(areas), nsub, nparts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--strict', action='store_true')
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    text, problems, (na, ns, npart) = build(root)
    for p in problems:
        print('warning:', p, file=sys.stderr)
    target = os.path.join(root, 'docs', 'FEATURE_MAP.md')
    if a.check:
        cur = open(target, encoding='utf-8').read() if os.path.exists(target) else ''
        if cur != text:
            print('docs/FEATURE_MAP.md is out of date: run python3 scripts/gen_feature_map.py', file=sys.stderr)
            return 1
        print(f'up to date ({na} areas, {ns} sub-areas, {npart} parts)')
        return 1 if (a.strict and problems) else 0
    with open(target, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print(f'wrote docs/FEATURE_MAP.md: {na} areas, {ns} sub-areas, {npart} parts, {len(problems)} warnings')
    return 1 if (a.strict and problems) else 0


if __name__ == '__main__':
    sys.exit(main())
