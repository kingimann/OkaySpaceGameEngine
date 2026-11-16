#!/usr/bin/env python3
"""Compare our own HTTP client's download (okay::ownhttp::DownloadToFile) with the curl program on a local server.

Usage: python3 scripts/own_http_bench.py <path to test_own_http_download> [size in MB=200] [runs=3] [out dir=$TMPDIR]

The server is test_own_http_download --serve (our own, plain sockets, loopback only). Each client runs as its own
process, so its wall time, CPU time (user + system, os.wait4) and peak resident memory (VmHWM, sampled) are its own.
Two pairs are compared: the transfer alone (our client with no hash and no fsync against plain curl), and a download
whose SHA-256 is checked (our client hashing while streaming against curl followed by a second pass with the same own
SHA-256, which is what a caller of curl has to do). The shipped default (hash, fsync, rename) is listed too. curl runs
with --noproxy '*' -s -o FILE. Every output is checked to be the same bytes. Nothing leaves the machine.
Standard library only.

The criterion printed at the end (docs/own_http_readiness.md): "met" when our client's median wall time is at most
110 % of curl's, its median CPU time at most 125 % of curl's and its peak memory at most curl's plus 8 MB.
"""
import hashlib, os, shutil, statistics, subprocess, sys, tempfile, time


def hwm_kb(pid):
    """Peak resident memory of a running process (VmHWM). ru_maxrss cannot be used: Linux carries the parent's
    (Python's) peak across fork and exec into it."""
    try:
        with open("/proc/%d/status" % pid) as f:
            for line in f:
                if line.startswith("VmHWM:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return 0


def run(argv):
    t0 = time.perf_counter()
    p = subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    peak = 0
    while True:   # sample the peak until the process is gone (VmHWM only grows, so the last sample is near the end)
        pid, status, ru = os.wait4(p.pid, os.WNOHANG)
        if pid:
            break
        peak = max(peak, hwm_kb(p.pid))
        time.sleep(0.005)
    p.returncode = os.waitstatus_to_exitcode(status)
    wall = time.perf_counter() - t0
    if p.returncode != 0:
        raise SystemExit("failed: %s" % " ".join(argv))
    return wall, ru.ru_utime + ru.ru_stime, peak / 1024.0


def chain(*runs):
    return (sum(r[0] for r in runs), sum(r[1] for r in runs), max(r[2] for r in runs))


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    tool = os.path.abspath(sys.argv[1])
    mb = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    runs = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    base = sys.argv[4] if len(sys.argv) > 4 else os.environ.get("TMPDIR", "/tmp")
    curl = shutil.which("curl")
    if not curl:
        raise SystemExit("curl is not installed")
    work = tempfile.mkdtemp(prefix="okay_http_bench_", dir=base)
    srv = subprocess.Popen([tool, "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        port = int(srv.stdout.readline())
        url = "http://127.0.0.1:%d/file?size=%d" % (port, mb << 20)
        own = lambda o, f="": run([tool, "--get", url, o] + ([f] if f else []))
        crl = lambda o: run([curl, "-s", "--noproxy", "*", "-o", o, url])
        clients = {
            # the transfer alone: no hash, no fsync (what plain curl does)
            "own transfer only": lambda o: own(o, "nosync,nohash"),
            "curl transfer only": crl,
            # what the launcher does: download, then check the SHA-256 (curl: a second pass with the same own hash)
            "own + SHA-256": lambda o: own(o, "nosync"),
            "curl + SHA-256 after": lambda o: chain(crl(o), run([tool, "--sha", o])),
            # as shipped: hash while streaming, fsync, rename
            "own + SHA-256 + fsync": lambda o: own(o),
        }
        res = {k: [] for k in clients}
        digest = None
        for i in range(runs):
            for name, argv in clients.items():   # interleaved, so a slow moment hits every client alike
                out = os.path.join(work, "out.bin")
                res[name].append(argv(out))
                d = sha256_file(out)
                digest = digest or d
                if d != digest:
                    raise SystemExit("%s produced different bytes" % name)
                os.unlink(out)
        print("download of %d MB from a local server, %d runs each (median; peak memory is the largest)" % (mb, runs))
        print("%-26s %10s %10s %12s %10s" % ("client", "wall s", "CPU s", "peak MB", "MB/s"))
        med = {}
        for name, rows in res.items():
            w = statistics.median(r[0] for r in rows)
            c = statistics.median(r[1] for r in rows)
            m = max(r[2] for r in rows)
            med[name] = (w, c, m)
            print("%-26s %10.3f %10.3f %12.1f %10.0f" % (name, w, c, m, mb / w))
        for name, ref in (("own transfer only", "curl transfer only"), ("own + SHA-256", "curl + SHA-256 after")):
            w, c, m = med[name]
            cw, cc, cm = med[ref]
            ok_w, ok_c, ok_m = w <= cw * 1.10, c <= cc * 1.25, m <= cm + 8
            print("%s vs %s: wall %.0f%% (%s), CPU %.0f%% (%s), memory %+.1f MB (%s) -> %s" % (
                name, ref, 100 * w / cw, "met" if ok_w else "not met", 100 * c / cc, "met" if ok_c else "not met",
                m - cm, "met" if ok_m else "not met", "MET" if (ok_w and ok_c and ok_m) else "NOT MET"))
    finally:
        try:
            srv.stdin.close()
        except OSError:
            pass
        srv.wait(timeout=30)
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
