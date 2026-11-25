#!/usr/bin/env python3
"""Package a built OkaySpace web player and a game into a folder a web host can serve (our own code).

    python3 scripts/web_package.py --build build-web/web --game path/to/game_folder --out dist-web [--zip game-web.zip]

Input: the Emscripten output (okay-player.html, .js, .wasm) and the exported game folder (game.okayscene,
game.okayconfig and its assets, as the editor's Build Settings > Web export writes it).
Output folder:
    index.html            the page (platform/web/shell.html with the game's config filled in)
    okay-player.js        the runtime
    okay-player.wasm.gz   the program, gzip-compressed (unpacked by the browser's own DecompressionStream)
    game.okaypack.gz      every game file in one pack, gzip-compressed; written into the virtual disk before main()
The pack format is written here and read by platform/web/okay_web.js:
    b"OKWPACK1", u32 little-endian manifest length, manifest JSON {"files": [{"path", "offset", "size"}]}, file bytes.
--zip also writes a zip with index.html at its root, the layout itch.io and similar hosts ask for.
Prints one size line per file and a total; --json prints the same as JSON.
"""
import argparse
import gzip
import json
import os
import struct
import sys
import zipfile

MAGIC = b"OKWPACK1"
CONFIG_MARK = "/*OKAY_WEB_CONFIG*/null"


def read_config(game_dir):
    """The game.okayconfig keys (key=value lines) the page uses: title, company, width, height."""
    cfg = {}
    path = os.path.join(game_dir, "game.okayconfig") if game_dir else ""
    if path and os.path.isfile(path):
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                if "=" in line:
                    k, v = line.rstrip("\r\n").split("=", 1)
                    cfg[k.strip()] = v.strip()
    return cfg


def game_files(game_dir):
    """Every file under the game folder, sorted, as (path inside the pack, path on disk). Dot files are left out."""
    out = []
    for root, dirs, files in os.walk(game_dir):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for name in sorted(files):
            if name.startswith("."):
                continue
            disk = os.path.join(root, name)
            rel = os.path.relpath(disk, game_dir).replace(os.sep, "/")
            out.append(("/" + rel, disk))
    return out


def build_pack(files):
    """The pack bytes (not compressed) for [(pack path, disk path)]."""
    entries, blobs, offset = [], [], 0
    for path, disk in files:
        with open(disk, "rb") as f:
            data = f.read()
        entries.append({"path": path, "offset": offset, "size": len(data)})
        blobs.append(data)
        offset += len(data)
    manifest = json.dumps({"files": entries}, separators=(",", ":")).encode("utf-8")
    return MAGIC + struct.pack("<I", len(manifest)) + manifest + b"".join(blobs)


def read_pack(data):
    """Parse a pack back into {path: bytes}; the same rules as okay_web.js (used by the tests)."""
    if data[:8] != MAGIC:
        raise ValueError("not an OkaySpace pack")
    (mlen,) = struct.unpack("<I", data[8:12])
    manifest = json.loads(data[12:12 + mlen].decode("utf-8"))
    base = 12 + mlen
    return {e["path"]: data[base + e["offset"]: base + e["offset"] + e["size"]] for e in manifest["files"]}


def gz(data):
    """gzip with a fixed time stamp and no file name, so the same input gives the same bytes."""
    return gzip.compress(data, compresslevel=9, mtime=0)


def slug(s):
    out = "".join(c if c.isalnum() else "_" for c in s.lower()).strip("_")
    while "__" in out:
        out = out.replace("__", "_")
    return out[:60] or "game"


def package(build_dir, game_dir, out_dir, title="", company="", fit="fill", compress=True, zip_path=""):
    html_in = os.path.join(build_dir, "okay-player.html")
    js_in = os.path.join(build_dir, "okay-player.js")
    wasm_in = os.path.join(build_dir, "okay-player.wasm")
    for p in (html_in, js_in, wasm_in):
        if not os.path.isfile(p):
            raise FileNotFoundError("the web build is missing " + p + " (run scripts/build-web.sh first)")
    with open(html_in, encoding="utf-8") as f:
        html = f.read()
    if CONFIG_MARK not in html:
        raise ValueError(html_in + " was not built with platform/web/shell.html (no config mark in it)")
    os.makedirs(out_dir, exist_ok=True)

    cfg = read_config(game_dir)
    title = title or cfg.get("title", "") or "OkaySpace game"
    company = company or cfg.get("company", "")
    sizes = []

    def put(name, data):
        with open(os.path.join(out_dir, name), "wb") as f:
            f.write(data)
        sizes.append((name, len(data)))

    with open(wasm_in, "rb") as f:
        wasm = f.read()
    wasm_name = "okay-player.wasm.gz" if compress else "okay-player.wasm"
    wasm_out = gz(wasm) if compress else wasm
    packs = []
    if game_dir:
        files = game_files(game_dir)
        if not files:
            raise ValueError("the game folder " + game_dir + " has no files")
        raw = build_pack(files)
        pack_name = "game.okaypack.gz" if compress else "game.okaypack"
        pack_out = gz(raw) if compress else raw
        packs.append({"file": pack_name, "bytes": len(pack_out), "files": len(files), "unpacked": len(raw)})
    config = {
        "title": title,
        "gameId": slug((company + " " + title).strip()),
        "fit": fit,
        "width": int(cfg.get("width", "0") or 0),
        "height": int(cfg.get("height", "0") or 0),
        "wasm": wasm_name,
        "wasmBytes": len(wasm_out),
        "packs": [{"file": p["file"], "bytes": p["bytes"]} for p in packs],
    }
    # Inside a <script>: "</" must not appear in the JSON, or a title could end the script early.
    cfg_json = json.dumps(config, separators=(",", ":")).replace("</", "<\\/")
    for stale in ("okay-player.wasm", "okay-player.wasm.gz", "game.okaypack", "game.okaypack.gz"):
        p = os.path.join(out_dir, stale)
        if os.path.isfile(p):
            os.remove(p)
    put("index.html", html.replace(CONFIG_MARK, cfg_json, 1).encode("utf-8"))
    with open(js_in, "rb") as f:
        put("okay-player.js", f.read())
    put(wasm_name, wasm_out)
    if packs:
        put(packs[0]["file"], pack_out)

    report = {"out": out_dir, "files": [{"name": n, "bytes": b} for n, b in sizes],
              "total": sum(b for _, b in sizes), "wasmUncompressed": len(wasm),
              "packs": packs, "config": config}
    if zip_path:
        if os.path.dirname(zip_path):
            os.makedirs(os.path.dirname(zip_path), exist_ok=True)
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
            for name, _ in sizes:   # the .gz files are already packed: stored, not deflated again
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_STORED if name.endswith(".gz") else zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                with open(os.path.join(out_dir, name), "rb") as f:
                    z.writestr(info, f.read())
        report["zip"] = {"path": zip_path, "bytes": os.path.getsize(zip_path)}
    return report


def human(n):
    for unit in ("bytes", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return ("%d %s" % (n, unit)) if unit == "bytes" else ("%.1f %s" % (n, unit))
        n /= 1024.0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build", required=True, help="the Emscripten output folder (okay-player.html/.js/.wasm)")
    ap.add_argument("--game", default="", help="the exported game folder (game.okayscene, game.okayconfig, assets)")
    ap.add_argument("--out", required=True, help="the folder to write the hostable game into")
    ap.add_argument("--title", default="")
    ap.add_argument("--company", default="")
    ap.add_argument("--fit", choices=("fill", "aspect"), default="fill",
                    help="fill: the game fills the page; aspect: the largest box of the game's width:height")
    ap.add_argument("--no-compress", action="store_true", help="ship plain .wasm and pack (for hosts that gzip themselves)")
    ap.add_argument("--zip", default="", help="also write this zip (index.html at its root, for itch.io)")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    try:
        r = package(a.build, a.game, a.out, a.title, a.company, a.fit, not a.no_compress, a.zip)
    except (OSError, ValueError) as e:
        print("web_package: " + str(e), file=sys.stderr)
        return 1
    if a.json:
        print(json.dumps(r, indent=1))
        return 0
    for f in r["files"]:
        print("  %-22s %10s" % (f["name"], human(f["bytes"])))
    print("  %-22s %10s  (program unpacked: %s)" % ("total download", human(r["total"]), human(r["wasmUncompressed"])))
    if "zip" in r:
        print("  zip " + r["zip"]["path"] + " " + human(r["zip"]["bytes"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
