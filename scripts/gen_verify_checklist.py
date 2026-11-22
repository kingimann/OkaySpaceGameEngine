#!/usr/bin/env python3
"""Builds the owner's verification checklists in docs/verification/ from the feature map and the hand-written step files.

Inputs
  docs/feature_map/*.md              the feature map pages (parsed with scripts/gen_feature_map.py)
  docs/verification/steps/<G>.md     hand-written rows for the most important features of group <G>
Outputs (generated, do not edit)
  docs/verification/README.md        how to use, legend, how to report
  docs/verification/<NN>-<name>.md   one checklist per area group (split in parts if a file would pass the size limit)

Every area of the feature map gets one smoke-test row (id V-<G>-S<area>) built from the map: its sub-areas, the
editor windows named there, the "Ask me like" prompts and the Open notes. Hand-written rows (id V-<G>-NNN) add the exact
steps for the important features. Nothing here was run in a real window: every "you should see" line says so.

  python3 scripts/gen_verify_checklist.py                 rewrite the files
  python3 scripts/gen_verify_checklist.py --check         exit 1 if a file is out of date (also checks the row format)
  python3 scripts/gen_verify_checklist.py --check-labels  exit 1 if a hand-written step names a menu label, environment
                                                          variable or path that is not in the source tree

Step file format (one block per row):
  ### V-LA-001 Title of the feature
  - **Map:** 17.2, 17.1.2            map numbers the row belongs to (every one must exist in the map)
  - **Where:** optional one line: which program
  - **Steps:**
    1. First step. Names of menus, buttons and windows in **bold** must exist in the source (--check-labels).
    2. Second step.
  - **See:** what you should see
  - **Partial:** known gaps, from the map's Open note or the matching doc ("none known" if there are none)
  - **Link:** optional, a relative doc link
  - **Grounded:** yes (default) or no. "no" marks the row "(steps unverified)"
  - **Needs:** optional, e.g. a release that contains a change that is not merged yet
"""
import argparse, glob, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_feature_map as fm  # noqa: E402

LIMIT = 58000   # characters per file; a GitHub issue body holds 65,000

# code, file stem, title, map areas, one line about the group
GROUPS = [
    ('LA', '01-launcher-and-accounts', 'Launcher and accounts', [17, 25, 48, 49],
     'The launcher (`OkaySpace.exe`): sign in, updates, settings, Create/Play pages, friends, store pages, crash reports.'),
    ('ED', '02-editor-basics', 'Editor basics', [15, 22, 23, 24, 34, 54, 55],
     'The editor (`Tools\\OkayEngine.exe`): menus, panels, Play, saving, undo, plugins, debugging tools.'),
    ('SC', '03-scene-and-components', 'Scene and components', [1, 20, 21, 36, 44, 45],
     'Scenes, GameObjects, components, prefabs, assets, volumes and level tools.'),
    ('CH', '04-characters-and-animation', 'Characters and animation', [3, 4, 5, 30],
     'Models, player controllers, cameras, stances, animation, animator, IK.'),
    ('PH', '05-physics-vehicles-navigation', 'Physics, vehicles and navigation', [6, 9, 52],
     'Rigid bodies, colliders, joints, vehicles and seats, navmesh and AI movement.'),
    ('RN', '06-rendering-and-post-processing', 'Rendering and post-processing', [2, 26, 27, 29, 37, 38, 43, 50, 53],
     'Lighting, sky, fog, post-processing, materials, terrain, render backends, 2D tools, capture.'),
    ('AU', '07-audio-and-input', 'Audio and input', [7, 8],
     'Audio sources, mixer, spatial sound, and keyboard, mouse, gamepad and touch input.'),
    ('UI', '08-ui-and-scripting', 'In-game UI, scripting and visual scripting', [12, 13, 14, 31, 32],
     'UI widgets, scripts, the script editor, visual scripting, localization.'),
    ('GP', '09-gameplay-story-and-samples', 'Gameplay, story and samples', [10, 11, 35, 42, 46, 47, 51],
     'Dialogue, quests, cutscenes, inventory, weapons, survival, replays, mods and the example scenes.'),
    ('NW', '10-networking-and-multiplayer', 'Networking, multiplayer and anti-cheat', [16, 39],
     'Hosting and joining, the server browser, dedicated server, anti-cheat.'),
    ('BX', '11-build-export-and-updates', 'Build, export, updates and tooling', [18, 19, 28, 33, 41, 56, 57, 58],
     'Build Settings, export, the release and update flow, tests and tooling.'),
    ('OC', '12-own-code-switches', 'Own-code switches (off by default)', [40],
     'Our own replacements for third-party code. Every one is off by default and switched on by an environment variable.'),
]



def load_map(root):
    outputs, problems, _ = fm.build(root)
    pages = sorted(p for p in glob.glob(os.path.join(root, 'docs', 'feature_map', '*.md'))
                   if not os.path.basename(p).startswith('_') and os.path.basename(p) != 'README.md')
    areas = []
    for p in pages:
        areas += fm.parse_page(p, [])
    return {a['num']: a for a in areas}


def one_line(text, limit):
    return fm.short(text.replace('`', "'"), limit) if text else ''


def status_word(sub):
    return fm.status_word(sub['f'].get('Status', ''))


def parse_steps(path, problems):
    rows = []
    cur = None
    field = None
    rel = os.path.relpath(path)
    with open(path, encoding='utf-8') as f:
        for n, line in enumerate(f.read().split('\n'), 1):
            m = re.match(r'^### (V-[A-Z]{2}-\d{3}) (.+?)\s*$', line)
            if m:
                cur = {'id': m.group(1), 'title': m.group(2), 'map': [], 'steps': [], 'f': {}, 'line': n, 'file': rel}
                rows.append(cur); field = None
                continue
            if cur is None:
                continue
            fmatch = re.match(r'^- \*\*([A-Za-z]+):\*\*\s*(.*)$', line)
            if fmatch:
                field = fmatch.group(1)
                val = fmatch.group(2).strip()
                if field == 'Steps':
                    field = 'Steps'
                else:
                    cur['f'][field] = val
                continue
            if field == 'Steps' and re.match(r'^\s+\d+\.\s', line):
                cur['steps'].append(re.sub(r'^\s+\d+\.\s+', '', line).strip())
            elif field and field != 'Steps' and line.startswith('  ') and line.strip():
                cur['f'][field] = (cur['f'][field] + ' ' + line.strip()).strip()
            elif field == 'Steps' and line.startswith('   ') and line.strip() and cur['steps']:
                cur['steps'][-1] += ' ' + line.strip()
    for r in rows:
        r['map'] = [x.strip() for x in r['f'].get('Map', '').split(',') if x.strip()]
        for need in ('Map', 'See', 'Partial'):
            if not r['f'].get(need):
                problems.append(f"{r['file']}:{r['line']}: {r['id']} is missing '{need}'")
        if not 1 <= len(r['steps']) <= 4:
            problems.append(f"{r['file']}:{r['line']}: {r['id']} has {len(r['steps'])} steps (1 to 4 allowed)")
        g = r['f'].get('Grounded', 'yes').lower()
        if g not in ('yes', 'no'):
            problems.append(f"{r['file']}:{r['line']}: {r['id']} Grounded must be yes or no")
        r['grounded'] = g == 'yes'
    return rows


def map_ids(areas):
    ids = set()
    for a in areas.values():
        ids.add(str(a['num']))
        for s in a['subs']:
            ids.add(s['id'])
            for p in s['lists'].get('Parts', []):
                pm = re.match(r'(\d+\.\d+\.\d+) ', p)
                if pm:
                    ids.add(pm.group(1))
    return ids


def smoke_row(code, a):
    """One generated row for a whole area, from the map only."""
    subs = a['subs']
    built = [s for s in subs if status_word(s) != 'Planned']
    planned = [s for s in subs if status_word(s) == 'Planned']
    lines = []
    lines.append(f"- [ ] **V-{code}-S{a['num']}** Area {a['num']}: {a['name']} (smoke test, steps unverified)")
    lines.append(f"  - **Map:** [{a['num']}](../feature_map/{a['page']}), {len(subs)} sub-areas, {len(built)} built.")
    if built:
        lines.append('  - **Steps:** (steps unverified: taken from the map, not clicked) '
                     'open the program or window named for each sub-area below and try its main action.')
        for s in built:
            ed = s['f'].get('Editor', '-')
            where = '' if ed.lower().startswith('none') or ed.strip() in ('-', '') else ' Where: ' + one_line(ed, 110)
            lines.append(f"    - {s['id']} {one_line(s['title'], 60)} [{status_word(s)}].{where}")
    else:
        lines.append('  - **Steps:** nothing is built in this area yet, so there is nothing to try.')
    lines.append('  - **You should see:** each listed thing opens and does what its name says '
                 '(expected, not yet seen on Windows).')
    secs = ', '.join(s['id'] for s in built)
    lines.append(f"  - **Known gaps:** see [docs/feature_map/{a['page']}](../feature_map/{a['page']}) (area {a['num']}, sections {secs or 'none built'}): "
                 'the Open line of each section. Not copied here so it cannot go stale.')
    ask_src = [s['f'].get('Ask me like', '') for s in built if s['f'].get('Ask me like')]
    if ask_src:
        lines.append(f"  - **Ask me like:** {one_line(ask_src[0], 130)}")
    if planned:
        lines.append(f"  - **Not built yet (skip):** {', '.join(s['id'] for s in planned)}")
    return '\n'.join(lines)


def hand_row(r):
    flag = '' if r['grounded'] else ' (steps unverified)'
    lines = [f"- [ ] **{r['id']}** {r['title']}{flag}"]
    maps = ', '.join(r['map'])
    where = r['f'].get('Where', '')
    lines.append(f"  - **Map:** {maps}" + (f". **Where:** {where}" if where else ''))
    if r['f'].get('Needs'):
        lines.append(f"  - **Needs:** {r['f']['Needs']}")
    lines.append('  - **Steps:**' + ('' if r['grounded'] else ' (steps unverified: could not be confirmed against the code)'))
    for i, st in enumerate(r['steps'], 1):
        lines.append(f"    {i}. {st}")
    lines.append(f"  - **You should see (expected, not yet seen on Windows):** {r['f']['See']}")
    lines.append(f"  - **Known partial:** {r['f']['Partial']}")
    if r['f'].get('Link'):
        lines.append(f"  - **More:** {r['f']['Link']}")
    return '\n'.join(lines)


def group_header(code, stem, title, areas, intro, part, nparts, nrows, nhand):
    nm = f" (part {part} of {nparts})" if nparts > 1 else ''
    return '\n'.join([
        f"# Verify: {title}{nm}", '',
        f"{intro}", '',
        'Generated by `scripts/gen_verify_checklist.py` from the feature map and `docs/verification/steps/'
        f'{code}.md`; do not edit by hand. Read the [guide](README.md) first (legend and how to report).', '',
        f"Areas: {', '.join(str(a) for a in areas)}. Rows ids start `V-{code}-`. "
        f"`V-{code}-S<area>` rows are smoke tests built from the map; `V-{code}-NNN` rows have written steps.", ''])


def pack(header_fn, hand, smoke):
    """Split rows into parts that stay under LIMIT characters. Returns a list of lists of row texts."""
    parts, cur, size = [], [], 0
    for row in hand + smoke:
        add = len(row) + 2
        if cur and size + add > LIMIT - 2500:
            parts.append(cur); cur, size = [], 0
        cur.append(row); size += add
    if cur:
        parts.append(cur)
    return parts


def readme_text(files):
    out = ['# Verify every feature on the real Windows release', '',
           'You asked for a way to go through every feature and check it works on the release you really run. These pages are that '
           'list. Each line is a GitHub task-list checkbox, so you can tick it in an issue and the issue shows your progress.', '',
           '**Nothing here was run in a real window by us.** Every "You should see" line is *expected, not yet seen on Windows*. '
           'Where a step could not be matched to the code it says `(steps unverified)`.', '',
           '## How to use it', '',
           '1. Install the release (`OkaySpace-Setup.exe` or the zip). Start `OkaySpace.exe`; the editor is `Tools\\OkayEngine.exe`, '
           'the player is `Tools\\OkaySpacePlayer.exe`. [try_it.md](../try_it.md) (this file arrives with PR #887; the link is dangling until it merges) says where the example '
           'scenes are and how to switch the own-code options on.',
           '2. Open a checklist below (or its GitHub issue). Go row by row. Each row has a stable id such as `V-ED-012`.',
           '3. Do the **Steps**. Compare with **You should see**.',
           '4. If it works, tick the box. If it does not, leave the box empty and comment on the issue with the row id and what happened.',
           '5. Rows that name a **Needs** line only work on a release that contains that change.', '',
           '## Legend', '',
           '| Mark | Meaning |', '|---|---|',
           '| `- [ ]` | not checked yet. Tick it when the row works for you |',
           '| `V-XX-NNN` | a row with written steps (important or recent feature) |',
           '| `V-XX-S<area>` | a smoke test for a whole feature-map area, built from the map. Its steps are `(steps unverified)` because they come from the map, not from clicking |',
           '| `(steps unverified)` | we could not match every step to the code, or the row is a smoke test. Expect to explore |',
           '| Expected, not yet seen on Windows | all "You should see" lines. We have only read code and run headless tests |',
           '| Known partial | what the map or the docs say is missing or rough. A gap listed here is **not** a bug report |', '',
           '## How to report a failure', '',
           'Comment on the checklist issue (or open one) with:', '',
           '```', 'V-ED-012 failed', 'Release: <version from Help > About OkaySpace>', 'What I did: ...', 'What I saw: ...',
           'What I expected: ...', '```', '',
           'Screenshots help. One comment per row. The row id is how we find the feature in [the feature map](../FEATURE_MAP.md).', '',
           '## Checklists', '',
           '| File | Rows | Of which written steps | Smoke tests |', '|---|---|---|---|']
    for stem, title, nrows, nhand, nsmoke in files:
        out.append(f"| [{title}]({stem}.md) | {nrows} | {nhand} | {nsmoke} |")
    out += ['', '## Keeping it in sync', '',
            'Add or change a row by editing `docs/verification/steps/<GROUP>.md` (hand-written rows) or the feature map (smoke rows), then run '
            '`python3 scripts/gen_verify_checklist.py`. these are manual commands, not part of CI: run `python3 scripts/gen_verify_checklist.py --check-labels` before editing a checklist, and run the generator after big feature-map changes. '
            '`--check` reports a stale file; `--check-labels` reports a step that names a menu item, variable or path that is no longer in the source. '
            'Every new feature must add its row here, as well as to [try_it.md](../try_it.md) and the feature map (CLAUDE.md rule 5).', '']
    return '\n'.join(out)


def build(root):
    problems = []
    areas = load_map(root)
    ids = map_ids(areas)
    seen = {}
    for g in GROUPS:
        for n in g[3]:
            if n in seen:
                problems.append(f'area {n} is in two groups')
            seen[n] = g[0]
    for n in areas:
        if n not in seen:
            problems.append(f'feature map area {n} ({areas[n]["name"]}) is in no verification group: add it to GROUPS')
    for n in seen:
        if n not in areas:
            problems.append(f'group lists area {n} which is not in the feature map')
    outputs, files, rowinfo = {}, [], {}
    all_hand = {}
    for code, stem, title, nums, intro in GROUPS:
        path = os.path.join(root, 'docs', 'verification', 'steps', code + '.md')
        hand = parse_steps(path, problems) if os.path.exists(path) else []
        for r in hand:
            if not r['id'].startswith(f'V-{code}-'):
                problems.append(f"{r['file']}:{r['line']}: {r['id']} does not start with V-{code}-")
            if r['id'] in all_hand:
                problems.append(f"duplicate row id {r['id']}")
            all_hand[r['id']] = r
            for mid in r['map']:
                if mid not in ids:
                    problems.append(f"{r['file']}:{r['line']}: {r['id']} names map id {mid} which does not exist")
        hand_txt = [hand_row(r) for r in sorted(hand, key=lambda r: r['id'])]
        smoke_txt = [smoke_row(code, areas[n]) for n in sorted(nums) if n in areas]
        parts = pack(None, hand_txt, smoke_txt)
        for i, rows in enumerate(parts, 1):
            name = stem + ('' if len(parts) == 1 else f'-part{i}')
            nh = sum(1 for r in rows if not re.match(r'- \[ \] \*\*V-[A-Z]{2}-S', r))
            head = group_header(code, stem, title, nums, intro, i, len(parts), len(rows), nh)
            text = head + '\n' + '\n\n'.join(rows) + '\n'
            outputs[f'docs/verification/{name}.md'] = text
            t = title + (f' (part {i} of {len(parts)})' if len(parts) > 1 else '')
            files.append((name, t, len(rows), nh, len(rows) - nh))
            rowinfo[name] = (len(text), len(rows), nh, rows)
    for p in glob.glob(os.path.join(root, 'docs', 'verification', 'steps', '*.md')):
        c = os.path.basename(p)[:-3]
        if c not in [g[0] for g in GROUPS]:
            problems.append(f'steps file {os.path.basename(p)} does not match any group')
    outputs['docs/verification/README.md'] = readme_text(files)
    return outputs, problems, rowinfo, all_hand, areas


LABEL_DIRS = ('editor', 'launcher', 'shared', 'engine', 'okayui', 'player', 'scripts', 'CMakeLists.txt', 'examples')
SRC_EXT = ('.cpp', '.hpp', '.h', '.inc', '.txt', '.py', '.sh', '.cmake', '.okayscene', '.okayui', '.c')


def source_text(root):
    chunks = []
    for d in LABEL_DIRS:
        base = os.path.join(root, d)
        if os.path.isfile(base):
            chunks.append(open(base, encoding='utf-8', errors='replace').read()); continue
        for dp, dn, fn in os.walk(base):
            dn[:] = [x for x in dn if x not in ('vendor', 'third_party', 'build')]
            for f in sorted(fn):
                if f.endswith(SRC_EXT):
                    try:
                        chunks.append(open(os.path.join(dp, f), encoding='utf-8', errors='replace').read())
                    except OSError:
                        pass
    return '\n'.join(chunks)


def check_labels(root, rows):
    src = source_text(root)
    bad = []
    for r in rows.values():
        if not r['grounded']:
            continue
        text = ' '.join(r['steps'] + [r['f'].get('Where', '')])
        for b in re.findall(r'\*\*([^*]+)\*\*', text):
            for part in re.split(r'\s+>\s+', b):
                part = part.strip().rstrip('.').rstrip('…').strip()
                part = re.sub(r'\.\.\.$', '', part).strip()
                if not part or len(part) < 2 or part.lower() in ('play', 'stop'):
                    pass
                if part and part not in src:
                    bad.append(f"{r['id']}: menu or label '{part}' is not in the source")
        for v in re.findall(r'`(OKAY_[A-Z0-9_]+)', text):
            if v not in src:
                bad.append(f"{r['id']}: variable {v} is not in the source")
        if '#887' in r['f'].get('Needs', ''):
            continue_paths = False
        else:
            continue_paths = True
        for pth in (re.findall(r'`([A-Za-z0-9_./\\-]+\.(?:okayscene|okayprefab|hpp|cpp|md|wav|off|dae|json|py|sh))`', text) if continue_paths else []):
            q = pth.replace('\\', '/')
            q = re.sub(r'^(?:<[^>]+>/)?', '', q)
            if q.startswith('examples/') or q.startswith('docs/') or q.startswith('scripts/') or '/' in q:
                if not os.path.exists(os.path.join(root, q)) and not q.startswith(('Tools/', 'Data/', 'exports/')):
                    bad.append(f"{r['id']}: path {pth} does not exist in the repository")
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--check-labels', action='store_true')
    ap.add_argument('--stats', action='store_true')
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    outputs, problems, rowinfo, hand, areas = build(root)
    for p in problems:
        print('error:', p, file=sys.stderr)
    for name, (size, nrows, nh, rows) in rowinfo.items():
        if size > 60000:
            print(f'error: {name}.md is {size} characters (limit 60000)', file=sys.stderr)
            problems.append('too big')
    if a.check_labels:
        bad = check_labels(root, hand)
        for b in bad:
            print('error:', b, file=sys.stderr)
        if bad or problems:
            return 1
        print(f'labels ok ({sum(1 for r in hand.values() if r["grounded"])} grounded rows checked)')
        return 0
    stale = []
    for rel, text in sorted(outputs.items()):
        target = os.path.join(root, rel)
        cur = open(target, encoding='utf-8').read() if os.path.exists(target) else None
        if cur != text:
            stale.append(rel)
            if not a.check:
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with open(target, 'w', encoding='utf-8', newline='\n') as f:
                    f.write(text)
    if a.stats:
        for name, (size, nrows, nh, rows) in rowinfo.items():
            print(f'{name}.md {size} chars {nrows} rows ({nh} written, {nrows - nh} smoke)')
        ng = sum(1 for r in hand.values() if r['grounded'])
        print(f'hand-written rows {len(hand)}: grounded {ng}, steps unverified {len(hand) - ng}; smoke rows {len(areas)}')
    if problems:
        return 1
    if a.check:
        if stale:
            print('out of date: ' + ', '.join(stale) + ': run python3 scripts/gen_verify_checklist.py', file=sys.stderr)
            return 1
        print(f'up to date ({len(outputs)} files, {len(hand)} written rows, {len(areas)} smoke rows)')
        return 0
    print(f'wrote {len(outputs)} files ({len(hand)} written rows, {len(areas)} smoke rows)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
