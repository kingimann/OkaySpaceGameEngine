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
  python3 scripts/gen_feature_map.py --validate check the hand-written pages only (exit 1 on any format problem, such
                                                as a missing What/Code/Status/Words line or a bad System line); writes
                                                nothing and does not compare the generated files. This is what PRs and
                                                scripts/precheck.sh run: the generated files are rebuilt on main by
                                                .github/workflows/regen-docs.yml, so PRs do not commit them.
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
    with open(path, encoding='utf-8') as f:
        return parse_page_text(os.path.basename(path), f.read(), problems)


def parse_page_text(rel, text, problems):
    """The areas of one page, from its text (rel is the page's file name)."""
    h1, areas = [], []
    area = sub = field = None
    mode = None
    lines = text.split('\n')
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
                elif field == 'System' and sub['lists'].get(field) and line.startswith((' ', '\t')):
                    sub['lists'][field][-1] += ' ' + st  # Home:/Entry:/... on the next line of a listed system
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
    """The leading status word of a Status line ("Works for the cursor..." -> "Works"); also used by
    gen_verify_checklist.py. Anything else gives the text before " - " (or "?" when empty)."""
    t = text.split(' - ')[0].strip()
    for w in STATUS_WORDS:
        if t.startswith(w):
            return w
    return t or '?'


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


SYS_KEY_RE = re.compile(r'(?:^|\s)(Code|Home|Entry|Doc|Also called|Fix):\s')
# Folders that hold one side of many systems by design (architecture.md): an editor window, a component wrapper.
SIDE_HOMES = ('editor', 'okay/Components')
BANNED_NAME_WORDS = ('own', 'new', 'improved', 'classic', 'v2')  # CLAUDE.md rule 19e; scripts/dup_lint.py checks file names
DEFAULT_FIX = 'Pick one home folder and move the rest there (docs/engine/modularization_plan.md)'


def parse_system(value):
    """One System value -> {'name', 'desc', 'Code', 'Home', 'Entry', 'Doc', 'Also called', 'Fix'} (keys only when given).
    Format: `Name - what it does Code: ... Home: `a/`, `b/` Entry: `a/X.hpp` Doc: `docs/x.md` Also called: old, other`."""
    keys = list(SYS_KEY_RE.finditer(value))
    head = value[:keys[0].start()] if keys else value
    out = {}
    for i, m in enumerate(keys):
        end = keys[i + 1].start() if i + 1 < len(keys) else len(value)
        out[m.group(1)] = value[m.end():end].strip().rstrip('.').strip()
    name, _, desc = head.partition(' - ')
    out['name'], out['desc'] = name.strip(), desc.strip()
    return out


def system_paths(value):
    """`a/`, `b/X.hpp` (planned) -> [('a/', False), ('b/X.hpp', True)]: (path, planned)."""
    res = []
    for item in value.split(','):
        m = re.search(r'`([^`]+)`', item)
        if m:
            res.append((m.group(1).strip(), 'planned' in item.lower()))
    return res


def split_aliases(value):
    return [a.strip().strip('`').strip() for a in value.split(',') if a.strip().strip('`').strip() not in ('', '-')]


def path_exists(root, p):
    if any(c in p for c in '*?['):
        return bool(glob.glob(os.path.join(root, p)))
    return os.path.exists(os.path.join(root, p))


def home_key(p):
    """The folder a home path belongs to: okay/<Folder> for engine/include/okay/ and engine/src/, else the top folder."""
    p = p.strip('/')
    for pre in ('engine/include/okay/', 'engine/src/'):
        if p.startswith(pre):
            return 'okay/' + p[len(pre):].split('/')[0]
    return p.split('/')[0]


def home_keys(paths):
    """Distinct homes of a system; an editor window or a component wrapper next to a subsystem folder is not a second home."""
    keys = list(dict.fromkeys(home_key(p) for p in paths))
    main = [k for k in keys if k not in SIDE_HOMES]
    return main or keys[:1]  # an editor tool with its component: one home


def lane_of(root, path):
    """Owner lane of a path from docs/lanes.json (scripts/collab_lib.py); '-' when there is no lanes file."""
    lp = os.path.join(root, 'docs', 'lanes.json')
    if not path or not os.path.exists(lp):
        return '-'
    import collab_lib as L
    lanes = L.load_lanes(root)
    owner = L.owner_of(path, lanes)
    return L.lane_display(owner, lanes) if owner in L.lane_defs(lanes) else owner


def first_doc(root, sub):
    for tok in re.findall(r'`([^`]+)`', sub['f'].get('Docs', '')):
        tok = tok.split('#')[0].strip()
        if tok.endswith('.md') and os.path.exists(os.path.join(root, tok)):
            return tok
    return ''


def collect_systems(root, areas, problems):
    """The system registry: {name: entry} and the order they were found. Each entry has name, desc, home (paths), entry,
    doc, lane, aliases, fix, subs [(area, sub)], code {sub id: Code value}, dup (phrase, target, note) or None, homes
    (distinct home folders). Used by docs/SYSTEMS.md, scripts/find_existing.py and scripts/dup_lint.py."""
    systems, order = {}, []
    for a in areas:
        for s in a['subs']:
            vals = system_entries(s)
            if not vals:
                problems.append(f"{s['page']}:{s['line']}: {s['id']} has no 'System' line")
            for v in vals:
                f = parse_system(v)
                name, desc = f['name'], f['desc']
                e = systems.get(name)
                if e is None:
                    e = systems[name] = {'name': name, 'desc': '', 'home': [], 'planned': set(), 'entry': '', 'doc': '',
                                         'aliases': [], 'fix': '', 'home_sub': None, 'subs': [], 'code': {}}
                    order.append(name)
                if 'Code' in f:  # this system's own files when a sub-area holds several systems
                    e['code'][s['id']] = f['Code']
                elif len(vals) > 1:
                    problems.append(f"{s['page']}:{s['line']}: {s['id']} holds several systems; give '{name}' "
                                    "its own ' Code: `...`' list")
                for key, attr in (('Entry', 'entry'), ('Doc', 'doc'), ('Fix', 'fix')):
                    if key in f:
                        val = f[key]
                        if key != 'Fix':
                            m = re.search(r'`([^`]+)`', val)
                            val = m.group(1).strip() if m else val
                        if e[attr] and e[attr] != val:
                            problems.append(f"{s['page']}:{s['line']}: system '{name}' has two different '{key}:' values")
                        e[attr] = e[attr] or val
                if 'Home' in f:
                    for p, planned in system_paths(f['Home']):
                        if p not in e['home']:
                            e['home'].append(p)
                        if planned:
                            e['planned'].add(p)
                if 'Also called' in f:
                    e['aliases'] += [x for x in split_aliases(f['Also called']) if x not in e['aliases']]
                if desc:
                    if e['home_sub'] is not None:
                        problems.append(f"{s['page']}:{s['line']}: system '{name}' is described twice "
                                        f"({e['home_sub'][1]['id']} and {s['id']})")
                    else:
                        e['desc'], e['home_sub'] = desc, (a, s)
                e['subs'].append((a, s))
    owners = {}
    for name in order:
        for k in [name] + systems[name]['aliases']:
            owners.setdefault(k.lower(), []).append(name)
    for name in order:
        e = systems[name]
        if e['home_sub'] is None:
            a, s = e['subs'][0]
            problems.append(f"{s['page']}:{s['line']}: system '{name}' has no ' - what it does' line anywhere")
            e['home_sub'], e['desc'] = (a, s), ''
        hs = e['home_sub'][1]
        where = f"{hs['page']}:{hs['line']}"
        for key, attr in (('Home', 'home'), ('Entry', 'entry')):
            if not e[attr]:
                problems.append(f"{where}: system '{name}' has no '{key}:' (registry format: docs/feature_map/README.md)")
        for p in e['home'] + ([e['entry']] if e['entry'] else []):
            if p not in e['planned'] and not path_exists(root, p):
                problems.append(f"{where}: system '{name}' names `{p}`, which does not exist (mark it 'planned' if it is)")
        if e['doc'] and not path_exists(root, e['doc']):
            problems.append(f"{where}: system '{name}' Doc `{e['doc']}` does not exist")
        e['doc'] = e['doc'] or first_doc(root, hs)
        for al in e['aliases']:
            others = [n for n in owners.get(al.lower(), []) if n != name]
            if others:
                problems.append(f"{where}: '{al}' is a name or alias of both '{name}' and '{others[0]}'; one name, one system")
        q = next((w for w in re.findall(r'[A-Za-z0-9]+', name) if w.lower() in BANNED_NAME_WORDS), None)
        if q:
            problems.append(f"{where}: system name '{name}' has '{q}' in it; one plain name per system (CLAUDE.md rule 19e)")
        e['lane'] = lane_of(root, e['entry'] or (e['home'][0] if e['home'] else ''))
        e['homes'] = home_keys([p for p in e['home'] if p not in e['planned']])
        e['dup'] = None
        for ph in DUP_PHRASES:
            i = e['desc'].find(' - ' + ph + ' ')
            if i >= 0:
                rest = e['desc'][i + len(ph) + 4:]
                target = next((n for n in sorted(systems, key=len, reverse=True) if rest.startswith(n)), None)
                if target is None or target == name:
                    problems.append(f"{where}: system '{name}' says '{ph}' but names no other system")
                else:
                    e['dup'] = (ph, target, rest[len(target):].strip())
                    e['desc'] = e['desc'][:i].strip()
                break
    return systems, order


def registry(root, pages=None):
    """The system registry for other tools: a list of system entries (see collect_systems) in feature-map order.
    pages: {file name: text} to read instead of docs/feature_map/*.md (dup_lint reads them from a git commit)."""
    problems, areas = [], []
    if pages is None:
        pages = {}
        for p in sorted(glob.glob(os.path.join(root, 'docs', 'feature_map', '*.md'))):
            with open(p, encoding='utf-8') as fh:
                pages[os.path.basename(p)] = fh.read()
    for rel in sorted(pages):
        if not rel.startswith('_') and rel != 'README.md':
            areas += parse_page_text(rel, pages[rel], problems)
    areas.sort(key=lambda a: a['num'])
    systems, order = collect_systems(root, areas, [])
    return [systems[n] for n in order]


def paths_cell(paths, planned=()):
    return ', '.join(f"`{p}`" + (' (planned)' if p in planned else '') for p in paths) or '-'


def doc_cell(doc):
    if not doc:
        return '-'
    return f"[{doc[5:]}]({doc[5:]})" if doc.startswith('docs/') else f"`{doc}`"


def build_systems(root, areas, problems, link):
    """docs/SYSTEMS.md from the System lines. Returns (text, systems)."""
    systems, order = collect_systems(root, areas, problems)
    lines = ['# Systems', '',
             'Generated from the `System:` lines of the feature map pages by `scripts/gen_feature_map.py`; do not edit.',
             'Back to the [feature map](FEATURE_MAP.md).', '',
             'This is **the one list of systems** (the registry). A **system** is a named part of OkaySpace that does one '
             'job from start to end (Physics, Save Games, OkayScript, the Launcher Updater). Each system has one official '
             'name, one home folder, one entry file, one doc and one owner lane, and is made of one or more sub-areas of '
             'the feature map.', '',
             '**Before you make anything**, look here: `python3 scripts/find_existing.py <words>` searches the names and '
             'the old names below first. If a system does the job, add on to it (CLAUDE.md rules 19c, 19d and 19e; '
             '[docs/process/no_duplicates.md](process/no_duplicates.md)).', '',
             '- **Home** is the folder (or files) that hold the system\'s own code; new code for it goes there. An editor '
             'window or a component for it is not a second home.',
             '- **Entry** is the file to open first: the main header, program or script.',
             '- **Doc** is the one guide for it. **Lane** is who owns the entry file (`docs/lanes.json`).',
             '- **Also called** lists old and other names it has had, frozen names (scene keys, env vars, CMake options, '
             'class names) and the third-party part it replaces, so a search for any of them lands here. Use the official '
             'name in new code, docs and messages (naming rule: [engine/architecture.md](engine/architecture.md#naming)).',
             '- **Status** comes from the sub-areas\' Status lines (Works, Partial, Basic, Planned, In flight); a system made '
             'of sub-areas in different states lists how many are in each.',
             '- **Feature map** links every sub-area that is part of it; the first one is the one that describes it.',
             '- To name or change a system, edit the `- **System:**` line of a sub-area in `docs/feature_map/` '
             '(format in `docs/feature_map/README.md`), check it with `python3 scripts/gen_feature_map.py --validate`; this page is rebuilt on main after the merge.', '']
    byarea = {}
    for name in order:
        byarea.setdefault(systems[name]['home_sub'][0]['num'], []).append(systems[name])
    lines += ['## Areas', '']
    for a in areas:
        if a['num'] in byarea:
            lines.append(f"- [{a['num']}. {a['name']}](#{slug(str(a['num']) + ' ' + a['name'])}): "
                         + ', '.join(e['name'] for e in sorted(byarea[a['num']], key=lambda e: idkey(e['home_sub'][1]['id']))))
    lines += ['- [Still duplicated](#still-duplicated)', '']
    for a in areas:
        if a['num'] not in byarea:
            continue
        lines += [f"## {a['num']}. {a['name']}", '',
                  '| System | What it does | Home | Entry | Doc | Lane | Also called | Status | Feature map |',
                  '|---|---|---|---|---|---|---|---|---|']
        for e in sorted(byarea[a['num']], key=lambda e: idkey(e['home_sub'][1]['id'])):
            home = e['home_sub'][1]
            subs = [home] + sorted((s for _, s in e['subs'] if s is not home), key=lambda s: idkey(s['id']))
            counts = {}
            for s in subs:
                w = short(status_word(s['f'].get('Status', '')), 30)
                counts[w] = counts.get(w, 0) + 1
            if len(counts) == 1:
                status = next(iter(counts))
            else:
                rank = {w: i for i, w in enumerate(STATUS_WORDS)}
                status = ', '.join(f"{n} {w}" for w, n in sorted(counts.items(), key=lambda kv: (-kv[1], rank.get(kv[0], 99), kv[0])))
            if e['dup'] or len(e['homes']) > 1:
                status += ' (see [Still duplicated](#still-duplicated))'
            pages = {s['id']: s['page'] for _, s in e['subs']}
            links = ', '.join(link(s['id'], pages[s['id']], s['title']) for s in subs)
            lines.append(f"| **{esc(e['name'])}** | {esc(e['desc'])} | {esc(paths_cell(e['home'], e['planned']))} | "
                         f"{esc(paths_cell([e['entry']] if e['entry'] else []))} | {doc_cell(e['doc'])} | {esc(e['lane'])} | "
                         f"{esc(', '.join(e['aliases']) or '-')} | {esc(status)} | {links} |")
        lines.append('')
    lines += ['## Still duplicated', '',
              'Systems that do the same job as another system, or whose code lives in more than one home folder. These '
              'are where a second copy is most likely to be made by someone who did not find the first: read the row '
              'before you touch either side. "Being merged" means a pull request is folding it into the other one. '
              'Rows come from `- duplicate of` / `- being merged into` on a System line, and from a `Home:` that names '
              'more than one folder (the fix is the `Fix:` text, or the default: pick one home). The code is not changed here.', '',
              '| System | Problem | Fix | Owner | Feature map |', '|---|---|---|---|---|']
    rows = []
    for name in order:
        e = systems[name]
        problem, fix = [], e['fix']
        if e['dup']:
            ph, target, note = e['dup']
            note = note.strip()
            if note.startswith('(') and note.endswith(')'):
                note = note[1:-1]
            problem.append(f"Same job as **{target}**")
            state = f"Being merged ({note})" if ph == 'being merged into' else (note[:1].upper() + note[1:] or 'Kept for now')
            fix = f"{state}. {fix}" if fix else state
        if len(e['homes']) > 1:
            problem.append('Code in ' + str(len(e['homes'])) + ' homes: ' + ', '.join(f"`{k}`" for k in e['homes']))
            fix = fix or DEFAULT_FIX
        if problem:
            home = e['home_sub'][1]
            rows.append((idkey(home['id']), f"| **{esc(name)}** | {esc('; '.join(problem))} | {esc(fix)} | {esc(e['lane'])} | "
                                             f"{link(home['id'], home['page'], home['title'])} |"))
    lines += [r for _, r in sorted(rows)] or ['| - | none recorded | - | - | - |']
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

    with open(os.path.join(root, 'docs', 'feature_map', '_header.md'), encoding='utf-8') as f:
        hdr = f.read().rstrip('\n')
    with open(os.path.join(root, 'docs', 'feature_map', '_footer.md'), encoding='utf-8') as f:
        ftr = f.read().strip('\n')
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
    for a in areas:  # system names and their old names (docs/SYSTEMS.md) lead to every sub-area of the system
        for s in a['subs']:
            for v in system_entries(s):
                add(parse_system(v)['name'], s['id'])
    sysmap, _ = collect_systems(root, areas, [])
    for e in sysmap.values():
        for al in e['aliases']:
            for _, s in e['subs']:
                add(al, s['id'])
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
    ap.add_argument('--validate', action='store_true')
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    outputs, problems, (na, ns, npart, nsys) = build(root)
    for p in problems:
        print('error:' if a.validate else 'warning:', p, file=sys.stderr)
    if a.validate:
        if problems:
            print(f'{len(problems)} problem(s) in docs/feature_map/*.md: fix the pages (format: docs/feature_map/README.md)',
                  file=sys.stderr)
            return 1
        print(f'pages valid ({na} areas, {ns} sub-areas, {npart} parts, {nsys} systems)')
        return 0
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
