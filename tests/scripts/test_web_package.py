#!/usr/bin/env python3
"""scripts/web_package.py on a made-up web build and game. Usage: test_web_package.py <web_package.py>

Checks the hostable folder it writes: index.html with the game's config in place of the shell's mark (and "</" escaped),
the program and the pack gzip-compressed and unchanged after unpacking, every game file in the pack at its path,
sizes in the config that match the files, the zip with index.html at its root and the .gz files stored, the same input
giving the same bytes, --no-compress, and the plain-words errors for a missing build and a shell without the mark."""
import gzip, importlib.util, json, os, subprocess, sys, tempfile, zipfile

SCRIPT = os.path.abspath(sys.argv[1])
spec = importlib.util.spec_from_file_location("web_package", SCRIPT)
wp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wp)

checks = failures = 0


def check(cond, what):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print("FAIL " + what)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def read(path):
    with open(path, "rb") as f:
        return f.read()


def run(*args):
    return subprocess.run([sys.executable, "-I", SCRIPT] + list(args), capture_output=True, text=True)


with tempfile.TemporaryDirectory() as tmp:
    build, game, out = os.path.join(tmp, "build"), os.path.join(tmp, "game"), os.path.join(tmp, "site")
    wasm = bytes(range(256)) * 400
    write(os.path.join(build, "okay-player.html"), "<html><script>var config = " + wp.CONFIG_MARK + " || {};</script></html>")
    write(os.path.join(build, "okay-player.js"), "var Module;")
    write(os.path.join(build, "okay-player.wasm"), wasm)
    write(os.path.join(game, "game.okayscene"), "okayscene 3\nname \"t\"\n")
    write(os.path.join(game, "game.okayconfig"), "title=My </script> Game\ncompany=Acme\nwidth=800\nheight=450\n")
    write(os.path.join(game, "Assets", "a b.png"), b"\x89PNG" + b"\x00" * 100)
    write(os.path.join(game, ".hidden"), "not shipped")

    r = run("--build", build, "--game", game, "--out", out, "--zip", os.path.join(tmp, "g.zip"), "--json")
    check(r.returncode == 0, "package exits 0: " + r.stderr)
    rep = json.loads(r.stdout)
    names = sorted(f["name"] for f in rep["files"])
    check(names == ["game.okaypack.gz", "index.html", "okay-player.js", "okay-player.wasm.gz"], "output files: %s" % names)
    check(rep["total"] == sum(os.path.getsize(os.path.join(out, n)) for n in names), "total is the sum of the files")
    check(gzip.decompress(read(os.path.join(out, "okay-player.wasm.gz"))) == wasm, "the program unpacks unchanged")
    check(os.path.getsize(os.path.join(out, "okay-player.wasm.gz")) < len(wasm), "the program is smaller packed")
    pack = wp.read_pack(gzip.decompress(read(os.path.join(out, "game.okaypack.gz"))))
    check(sorted(pack) == ["/Assets/a b.png", "/game.okayconfig", "/game.okayscene"], "pack paths: %s" % sorted(pack))
    check(pack["/game.okayscene"] == read(os.path.join(game, "game.okayscene")), "a file's bytes in the pack")
    check(pack["/Assets/a b.png"] == read(os.path.join(game, "Assets", "a b.png")), "a binary file's bytes in the pack")

    html = read(os.path.join(out, "index.html")).decode("utf-8")
    check(wp.CONFIG_MARK not in html, "the config mark is replaced")
    check("</script> Game" not in html, "a title cannot end the script early")
    cfg = json.loads(html.split("var config = ", 1)[1].split(" || {}", 1)[0])
    check(cfg["title"] == "My </script> Game", "title from game.okayconfig")
    check(cfg["gameId"] == "acme_my_script_game", "game id from company and title: " + cfg["gameId"])
    check(cfg["width"] == 800 and cfg["height"] == 450 and cfg["fit"] == "fill", "size and fit")
    check(cfg["wasm"] == "okay-player.wasm.gz" and cfg["wasmBytes"] == os.path.getsize(os.path.join(out, "okay-player.wasm.gz")), "program size in the config")
    check(cfg["packs"] == [{"file": "game.okaypack.gz", "bytes": os.path.getsize(os.path.join(out, "game.okaypack.gz"))}], "pack size in the config")

    with zipfile.ZipFile(os.path.join(tmp, "g.zip")) as z:
        infos = {i.filename: i for i in z.infolist()}
        check("index.html" in infos, "index.html at the zip's root")
        check(infos["okay-player.wasm.gz"].compress_type == zipfile.ZIP_STORED, ".gz stored in the zip, not packed twice")
        check(z.read("okay-player.wasm.gz") == read(os.path.join(out, "okay-player.wasm.gz")), "zip holds the same bytes")
    check(rep["zip"]["bytes"] == os.path.getsize(os.path.join(tmp, "g.zip")), "zip size reported")

    # the same input gives the same bytes (gzip and zip time stamps fixed)
    out2 = os.path.join(tmp, "site2")
    check(run("--build", build, "--game", game, "--out", out2, "--zip", os.path.join(tmp, "g2.zip")).returncode == 0, "second run")
    for n in names:
        check(read(os.path.join(out, n)) == read(os.path.join(out2, n)), "same bytes: " + n)
    check(read(os.path.join(tmp, "g.zip")) == read(os.path.join(tmp, "g2.zip")), "same zip")

    # --no-compress: plain files, and no stale .gz left behind in the same folder
    r = run("--build", build, "--game", game, "--out", out, "--no-compress", "--fit", "aspect")
    check(r.returncode == 0, "--no-compress exits 0")
    check(sorted(os.listdir(out)) == ["game.okaypack", "index.html", "okay-player.js", "okay-player.wasm"], "plain files: %s" % sorted(os.listdir(out)))
    check(read(os.path.join(out, "okay-player.wasm")) == wasm, "plain program unchanged")
    check('"fit":"aspect"' in read(os.path.join(out, "index.html")).decode(), "fit aspect in the config")
    check("total download" in r.stdout, "the human size report")

    # errors in plain words
    r = run("--build", os.path.join(tmp, "nothing"), "--out", out)
    check(r.returncode == 1 and "run scripts/build-web.sh first" in r.stderr, "missing build: " + r.stderr)
    write(os.path.join(build, "okay-player.html"), "<html>emcc's own shell</html>")
    r = run("--build", build, "--game", game, "--out", out)
    check(r.returncode == 1 and "platform/web/shell.html" in r.stderr, "wrong shell: " + r.stderr)
    write(os.path.join(build, "okay-player.html"), wp.CONFIG_MARK)
    os.makedirs(os.path.join(tmp, "empty"))
    r = run("--build", build, "--game", os.path.join(tmp, "empty"), "--out", out)
    check(r.returncode == 1 and "has no files" in r.stderr, "empty game: " + r.stderr)

    # the pack reader refuses what is not a pack
    try:
        wp.read_pack(b"NOTAPACK" + b"\x00" * 8)
        check(False, "read_pack refuses a non-pack")
    except ValueError:
        check(True, "")

print("%d checks, %d failures" % (checks, failures))
sys.exit(1 if failures else 0)
