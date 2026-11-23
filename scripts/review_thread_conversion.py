#!/usr/bin/env python3
"""Mechanical review of the launcher's move from SDL's thread, mutex and atomic calls to shared/OkayThreads.hpp (okay::thr).

Two checks:
  default (no git needed): the files hold no SDL thread / mutex / atomic call any more, and the number of uses of each okay::thr
      name equals the number of SDL uses recorded in tests/data/thread_conversion_baseline.json (taken from the last commit that still
      used SDL: 2eda734e). A call lost or added during the move changes a count.
  --base REV (needs git history): for every file, the old text gets the same name mapping applied and every line that holds a thread
      token must equal, in order, the line in the current file (whitespace ignored). Lines changed on purpose are listed in ALLOWED below
      and shown; anything else is a mismatch.
Exit 0 = clean. --write-baseline REV writes the baseline file from REV.
"""
import json, re, subprocess, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = ['launcher/main.cpp', 'launcher/VoiceChat.hpp', 'launcher/VersionsUI.cpp']
BASELINE = os.path.join(ROOT, 'tests', 'data', 'thread_conversion_baseline.json')
# launcher/main.cpp is being split into smaller files by pure moves (docs/engine/modularization_plan.md, Part C). The code moved out
# of it is counted with it, so a move keeps the counts. (--base lines the files up in this order, so it only matches for revisions
# from before the split.)
SPLIT_FROM = {'launcher/main.cpp': ['launcher/ChatText.cpp', 'launcher/AntiCheatPages.cpp', 'launcher/CoverArtAvatars.cpp',
                                     'launcher/NavIcons.cpp', 'launcher/NewsHistory.cpp', 'launcher/AdminPage.cpp',
                                     'launcher/PublicProfilePage.cpp',
                                     'launcher/MemberModerationUI.cpp', 'launcher/GroupConversation.cpp',
                                     'launcher/Multiplayer.cpp']}
MAP = [(r'\bSDL_CreateThread\b', 'okay::thr::CreateThread'), (r'\bSDL_DetachThread\b', 'okay::thr::DetachThread'),
       (r'\bSDL_WaitThread\b', 'okay::thr::WaitThread'), (r'\bSDL_CreateMutex\b', 'okay::thr::CreateMutex'),
       (r'\bSDL_DestroyMutex\b', 'okay::thr::DestroyMutex'), (r'\bSDL_LockMutex\b', 'okay::thr::LockMutex'),
       (r'\bSDL_UnlockMutex\b', 'okay::thr::UnlockMutex'), (r'\bSDL_AtomicSet\b', 'okay::thr::AtomicSet'),
       (r'\bSDL_AtomicGet\b', 'okay::thr::AtomicGet'), (r'\bSDL_AtomicAdd\b', 'okay::thr::AtomicAdd'),
       (r'\bSDL_AtomicCAS\b', 'okay::thr::AtomicCAS'), (r'\bSDL_Thread\b', 'okay::thr::Thread'),
       (r'\bSDL_mutex\b', 'okay::thr::Mutex'), (r'\bSDL_atomic_t\b', 'okay::thr::AtomicInt')]
# Changed by hand, on purpose: SDL_AtomicIncRef / DecRef (result unused at every site) became AtomicAdd(+1 / -1).
ALLOWED = [(r'SDL_AtomicIncRef\(&(\w+)\)', r'okay::thr::AtomicAdd(&\1, 1)'), (r'SDL_AtomicDecRef\(&(\w+)\)', r'okay::thr::AtomicAdd(&\1, -1)')]
TOKEN = re.compile(r'okay::thr::\w+|SDL_(Create|Detach|Wait|Destroy|Lock|Unlock)(Thread|Mutex)\b|SDL_Atomic\w*|SDL_atomic_t|SDL_mutex\b|SDL_Thread\b')
NEW_NAMES = re.compile(r'okay::thr::(\w+)')

def strip(s):
    s = re.sub(r'//[^\n]*', '', s)      # comments do not matter
    return s

def read(f): return strip(open(os.path.join(ROOT, f), encoding='utf-8').read())
def read_group(f): return read(f) + ''.join('\n' + read(g) for g in SPLIT_FROM.get(f, []))
def read_rev(rev, f): return strip(subprocess.check_output(['git', '-C', ROOT, 'show', '%s:%s' % (rev, f)]).decode('utf-8'))

def counts_old(text):
    c = {}
    for pat, new in MAP + [(a, b) for a, b in ALLOWED]:
        n = len(re.findall(pat, text))
        if n: c[new.split('(')[0]] = c.get(new.split('(')[0], 0) + n
    return c
def counts_new(text):
    c = {}
    for m in NEW_NAMES.finditer(text): c['okay::thr::' + m.group(1)] = c.get('okay::thr::' + m.group(1), 0) + 1
    return c

def main():
    if len(sys.argv) >= 3 and sys.argv[1] == '--write-baseline':
        out = {f: counts_old(read_rev(sys.argv[2], f)) for f in FILES}
        os.makedirs(os.path.dirname(BASELINE), exist_ok=True)
        json.dump(out, open(BASELINE, 'w'), indent=1, sort_keys=True); print('wrote', BASELINE); return 0
    bad = 0
    base = json.load(open(BASELINE))
    for f in FILES:
        new = read_group(f)
        left = [m.group(0) for m in re.finditer(r'SDL_(Create|Detach|Wait|Destroy|Lock|Unlock)(Thread|Mutex)\b|SDL_Atomic\w*|SDL_atomic_t|SDL_mutex\b|SDL_Thread\b', new)]
        if left: print('%s: SDL thread names still present: %s' % (f, sorted(set(left)))); bad += 1
        want, have = base[f], counts_new(new)
        for k in sorted(set(want) | set(have)):
            if want.get(k, 0) != have.get(k, 0):
                print('%s: %s: baseline %d, now %d' % (f, k, want.get(k, 0), have.get(k, 0))); bad += 1
    if len(sys.argv) >= 3 and sys.argv[1] == '--base':
        rev = sys.argv[2]
        for f in FILES:
            old = read_rev(rev, f)
            for a, b in MAP + ALLOWED: old = re.sub(a, b, old)
            ol = [re.sub(r'\s+', '', l) for l in old.split('\n') if TOKEN.search(l)]
            nl = [re.sub(r'\s+', '', l) for l in read_group(f).split('\n') if TOKEN.search(l)]
            if len(ol) != len(nl): print('%s: %d lines with thread names before, %d now' % (f, len(ol), len(nl))); bad += 1
            for i, (a, b) in enumerate(zip(ol, nl)):
                if a != b: print('%s: line %d differs\n  old: %s\n  new: %s' % (f, i + 1, a, b)); bad += 1
    print('thread conversion review: %s' % ('CLEAN' if not bad else '%d problem(s)' % bad))
    return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
