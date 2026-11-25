#!/usr/bin/env bash
# Survey: compile every engine/src/**/*.cpp alone with okay-cc --obj and record
# the first error of each file. Not part of CI. Needs tools/okaycc/build.sh first.
# Usage: tools/okaycc/survey.sh [result-file]      (default build/survey/result.txt)
#        tools/okaycc/survey.sh --needs             (which missing headers each file needs)
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
S="$HERE/build/survey"
CC="${OKAYCC:-$HERE/build/okay-cc}"     # OKAYCC=path picks another build of okay-cc
INC="${OKAYINC:-tools/okaycc/include}"  # and OKAYINC its headers
mkdir -p "$S"
cd "$ROOT"

if [[ "${1:-}" == "--needs" ]]; then
  # Stub every missing header (one line naming it), so preprocessing goes on,
  # and count how many files need each header we lack.
  python3 - "$ROOT" "$S" "$CC" "$INC" <<'EOF'
import collections, json, os, re, subprocess, sys
root, s, cc, inc = sys.argv[1:5]
stub = os.path.join(s, 'stub')
os.makedirs(stub, exist_ok=True)
files = sorted(subprocess.check_output(['find', 'engine/src', '-name', '*.cpp'], cwd=root, text=True).split())
needs = {}
for f in files:
    for _ in range(120):
        p = subprocess.run([cc, '--pp', '-I', 'engine/include', '-I', inc, '-I', stub, f],
                           cwd=root, capture_output=True, text=True)
        m = re.search(r'cannot find header "([^"]+)"', p.stderr)
        if not m:
            break
        d = os.path.join(stub, m.group(1))
        os.makedirs(os.path.dirname(d), exist_ok=True)
        open(d, 'w').write('OKAYSTUB "%s"\n' % m.group(1))
    needs[f] = sorted(set(re.findall(r'OKAYSTUB "([^"]+)"', p.stdout)))
    if p.returncode != 0:
        needs[f].append('PPERR:' + (p.stderr.splitlines()[0] if p.stderr else ''))
json.dump(needs, open(os.path.join(s, 'needs.json'), 'w'), indent=0)
c = collections.Counter(h for v in needs.values() for h in v)
for h, n in c.most_common():
    print(n, h)
print('zero-need files', sum(1 for v in needs.values() if not v))
EOF
  exit 0
fi

OUT="${1:-$S/result.txt}"
[[ "$OUT" = /* ]] || OUT="$OLDPWD/$OUT"
: > "$OUT"
find engine/src -name '*.cpp' | sort | while read -r f; do
  err=$(timeout 30 "$CC" --obj "$S/o.o" -I engine/include -I "$INC" "$f" 2>&1 >/dev/null; echo "rc=$?")
  rc=${err##*rc=}; err=$(echo "$err" | grep -v '^rc=' | head -1)
  if [[ $rc != 0 && -z "$err" ]]; then err="exit $rc (timeout or crash)"; fi
  if [[ -z "$err" ]]; then echo "OK|$f" >> "$OUT"; else echo "ERR|$f|$err" >> "$OUT"; fi
done
echo "total $(wc -l < "$OUT") ok $(grep -c '^OK' "$OUT")"
echo "first missing headers:"
grep '^ERR' "$OUT" | grep -o 'cannot find header "[^"]*"' | sort | uniq -c | sort -rn | head -15
echo "language errors: $(grep '^ERR' "$OUT" | grep -vc 'cannot find header')"
