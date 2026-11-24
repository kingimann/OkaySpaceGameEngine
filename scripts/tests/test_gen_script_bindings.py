#!/usr/bin/env python3
"""The script bindings never reach files, sockets, platform services or the clipboard (review of PR #1234, B1), and
count, index and enum arguments are range-checked (B2). Reads the model scripts/gen_script_bindings.py builds and
the file it wrote."""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import gen_script_bindings as g  # noqa: E402

fails = []


def check(cond, what):
    if not cond:
        fails.append(what)


check(g.words("filePath") == ["file", "path"], "words(filePath)")
check(g.words("ExportHeightmap") == ["export", "heightmap"], "words(ExportHeightmap)")
check(g.words("profile") == ["profile"], "words(profile) is not 'file'")
check(g.denied("X", "X", "Load", [("const std::string&", False, "path")]) != "", "a text path parameter is denied")
check(g.denied("X", "X", "Move", [("Vec3", False, "dir")]) == "", "a direction vector is not a path")
check(g.denied("X", "X", "OpenOverlay", []) != "", "an overlay is denied")
check(g.denied("NetworkManager", "NetworkManager", "IsServer", []) != "", "network classes are denied")

classes, global_enums = g.collect()
model, stats, _ = g.build_model()
for c in model:
    if c["name"] in g.DENY_CLASSES:
        check(not c["methods"], f"{c['name']} is a denied class but has bound methods")
    for m in c["methods"]:
        full = f"{c['name']}.{m['name']}"
        check(full not in g.DENY_METHODS, f"{full} is on the deny list but bound")
        check(not (set(g.words(m["name"])) & g.DENY_NAME_WORDS), f"{full}: a denied word in its name")
        for pname, k in zip(m["pnames"], m["params"]):
            if k[0] == "String":
                check(not (set(g.words(pname)) & g.DENY_PARAM_WORDS), f"{full}: text parameter '{pname}' names a place")
check(stats["denied"] > 0, "something is denied")

gen = open(g.OUT, encoding="utf-8").read()
check("ExportHeightmap" not in gen and "StartServer" not in gen and "OpenOverlay" not in gen and
      "PasteFromClipboard" not in gen, "known file/network/platform methods are absent from the generated file")
check(re.search(r"SetPositionCount\(static_cast<int>\(sb::ArgCount\(", gen) is not None, "counts are capped")
for line in gen.splitlines():
    m = re.match(r'\s*\{"\w+", \{([^}]*)\}', line)
    if m and "ArgKind::Enum" in m.group(1):
        check("sb::ArgIntIn(" in line, "every enum argument is range-checked: " + line.strip()[:80])

for f in fails:
    print("FAIL:", f)
print(f"script binding rules: {len(fails)} failures")
sys.exit(1 if fails else 0)
