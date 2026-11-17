#!/usr/bin/env python3
"""Check okay::crypto::Sha256 (both block functions) against Python's hashlib on many random messages, each fed in
random pieces. Usage: sha256_fast_check.py <path to sha256_compare> [messages=100000] [max length=10000] [seed=1]

Every message (random length 0 to max, random bytes) is cut into random pieces: empty updates, single bytes, pieces
around one block (63, 64, 65, 127, 128, 129 bytes) and larger random pieces, so the buffered path, the top-up path and
the many-blocks-at-once path are all used. The cases go to the tool through a pipe (nothing on disk). The tool is run
twice: once with the block function the CPU picks (the x86 SHA extensions when present) and once with
OKAY_SHA256_PORTABLE=1. Exit status 0 only when every digest in both runs equals hashlib's. Standard library only.
"""
import hashlib, os, random, struct, subprocess, sys


def cases(n, maxlen, seed):
    r = random.Random(seed)
    for i in range(n):
        size = r.randrange(0, maxlen + 1)
        m = r.randbytes(size)
        pieces, left = [], size
        while left > 0:
            kind = r.randrange(6)
            if kind == 0:
                p = 0
            elif kind == 1:
                p = r.randrange(1, 4)
            elif kind == 2:
                p = r.choice([63, 64, 65, 127, 128, 129])
            else:
                p = r.randrange(1, left + 1)
            p = min(p, left)
            pieces.append(p)
            left -= p
        if r.randrange(4) == 0:
            pieces.append(0)   # an empty update at the end too
        yield struct.pack("<II", size, len(pieces)) + m + struct.pack("<%dI" % len(pieces), *pieces) + hashlib.sha256(m).digest()


def run(tool, env_portable, n, maxlen, seed):
    env = dict(os.environ)
    if env_portable:
        env["OKAY_SHA256_PORTABLE"] = "1"
    else:
        env.pop("OKAY_SHA256_PORTABLE", None)
    p = subprocess.Popen([tool, "split"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=env)
    try:
        for c in cases(n, maxlen, seed):
            p.stdin.write(c)
        p.stdin.close()
    except BrokenPipeError:
        pass
    out = p.stdout.read().decode()
    p.wait()
    sys.stdout.write(out)
    return p.returncode


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    tool = os.path.abspath(sys.argv[1])
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 100000
    maxlen = int(sys.argv[3]) if len(sys.argv) > 3 else 10000
    seed = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    bad = 0
    for portable in (False, True):
        bad += run(tool, portable, n, maxlen, seed) != 0
    print("sha256_fast_check: %s" % ("PASS" if bad == 0 else "FAIL"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
