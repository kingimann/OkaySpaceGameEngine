#!/usr/bin/env python3
"""Lists source files that the feature map (docs/FEATURE_MAP.md and docs/feature_map/*.md) never mentions.
Usage: check_feature_map.py [repo_root] [--dir PREFIX ...] [--list]
A file counts as mapped when its path under engine/include/okay (no extension, e.g. Core/Scheduler),
its repo path, or its file stem as a whole word of at least 6 characters appears in the map text."""
import os, re, sys, glob
root = '.'
dirs = []
args = sys.argv[1:]
show = '--list' in args
args = [a for a in args if a != '--list']
i = 0
while i < len(args):
    if args[i] == '--dir': dirs.append(args[i+1]); i += 2
    else: root = args[i]; i += 1
texts = []
for p in [os.path.join(root, 'docs', 'FEATURE_MAP.md')] + glob.glob(os.path.join(root, 'docs', 'feature_map', '*.md')):
    if os.path.exists(p): texts.append(open(p, encoding='utf-8', errors='replace').read())
text = '\n'.join(texts)
words = set(re.findall(r'[A-Za-z0-9_]+', text))
SRC = ('.hpp', '.h', '.cpp', '.c', '.cc', '.py', '.sh', '.bat', '.cmake', '.nsi', '.ts', '.sql')
SKIP = ('third_party', '/archive/', 'build/', 'tests/assets', 'tests/data', 'tests/export', 'tests/golden')
roots = ['engine/include/okay', 'engine/src', 'editor', 'launcher', 'player', 'relay', 'sandbox', 'shared',
         'okayui', 'supabase', 'scripts', 'tools', 'examples', 'deploy', 'cmake', 'steam', 'tests', 'docs']
files = []
for r in roots:
    for dp, dn, fn in os.walk(os.path.join(root, r)):
        for f in fn:
            full = os.path.join(dp, f)
            rel = os.path.relpath(full, root).replace(os.sep, '/')
            if any(s in rel for s in SKIP): continue
            if rel in ('docs/FEATURE_MAP.md',) or rel.startswith('docs/feature_map/'): continue
            if rel.endswith(SRC) or rel.endswith('.md'): files.append(rel)
def mapped(rel):
    noext = os.path.splitext(rel)[0]
    inc = noext[len('engine/include/okay/'):] if noext.startswith('engine/include/okay/') else None
    stem = os.path.basename(noext)
    if inc and inc in text: return True
    if noext in text or rel in text: return True
    if os.path.basename(rel) in text: return True
    if rel.startswith('engine/src/') and rel[len('engine/src/'):].rsplit('.', 1)[0] in text: return True
    if len(stem) >= 6 and stem in words: return True
    if stem.startswith('test_') and stem[5:] in words and len(stem) >= 9: return True
    return False
un = [f for f in sorted(files) if (not dirs or any(f.startswith(d) for d in dirs)) and not mapped(f)]
tot = [f for f in files if (not dirs or any(f.startswith(d) for d in dirs))]
print(f"{len(tot)-len(un)}/{len(tot)} files mapped, {len(un)} unmapped")
by = {}
for f in un:
    k = '/'.join(f.split('/')[:3 if f.startswith('engine/') else 2])
    by.setdefault(k, []).append(f)
for k in sorted(by): print(f"  {k}: {len(by[k])}")
if show:
    for f in un: print(f)
