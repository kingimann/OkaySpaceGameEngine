#!/usr/bin/env python3
"""Generate the script bindings for engine components from our own headers.

Reads every public header under engine/include/okay, finds the classes that derive
(directly or through other engine classes) from okay::Component, and writes one
binding per public method whose argument and return types a script value can carry
(numbers, true/false, text, vectors, colours, enums, object references). Properties
are not generated here: they come from the property registry (PropertyRegistry.hpp,
BuiltinProperties*.cpp) at run time.

The output is engine/src/Scripting/ScriptBindingsGenerated.cpp. The hand-written half
of the layer (the handle, the dispatch, the type checks) is ScriptBindings.cpp.

Written for this engine from the C++ grammar of declarations; it is a small
declaration reader, not a C++ parser: anything it cannot read for sure is skipped
and counted, never guessed.

  python3 scripts/gen_script_bindings.py            # write the file
  python3 scripts/gen_script_bindings.py --check    # fail when the file is out of date
  python3 scripts/gen_script_bindings.py --audit    # print the gap numbers
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INCLUDE = os.path.join(ROOT, "engine", "include")
OUT = os.path.join(ROOT, "engine", "src", "Scripting", "ScriptBindingsGenerated.cpp")
EXCLUDE_FILE = os.path.join(ROOT, "scripts", "script_bindings_exclude.txt")

# Lifecycle and engine-internal callbacks: the engine calls these, a script must not.
SKIP_METHODS = {
    "Awake", "Start", "Update", "LateUpdate", "FixedUpdate", "OnRender", "OnDestroy", "ReleaseUnstarted",
    "OnEnable", "OnDisable", "OnPoolReuse", "OnObjectsDestroyed", "OnGUI", "OnDrawGizmos", "OnValidate",
    "OnCollisionEnter2D", "OnCollisionStay2D", "OnCollisionExit2D", "OnTriggerEnter2D", "OnTriggerStay2D",
    "OnTriggerExit2D", "OnCollisionEnter", "OnCollisionStay", "OnCollisionExit", "OnTriggerEnter",
    "OnTriggerStay", "OnTriggerExit", "Render", "RenderUI", "Serialize", "Deserialize", "Clone",
}

INT_TYPES = {
    "int", "unsigned", "unsigned int", "short", "long", "long long", "unsigned long", "unsigned long long",
    "std::size_t", "size_t", "std::uint8_t", "uint8_t", "std::uint16_t", "uint16_t", "std::uint32_t", "uint32_t",
    "std::uint64_t", "uint64_t", "std::int8_t", "int8_t", "std::int16_t", "int16_t", "std::int32_t", "int32_t",
    "std::int64_t", "int64_t", "char",
}
VALUE_TYPES = {"bool": "Bool", "float": "Float", "double": "Float", "std::string": "String",
               "Vec2": "Vec2", "Vec3": "Vec3", "Vec4": "Vec4", "Quat": "Quat", "Color": "Color"}


def strip_comments(text):
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
        elif c == '"' or c == "'":
            q, j = c, i + 1
            while j < n and text[j] != q:
                j += 2 if text[j] == "\\" else 1
            out.append(q + q)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def drop_conditionals(text):
    """Remove preprocessor lines, and everything inside #if blocks (a class there may not exist in every build)."""
    keep, depth = [], 0
    for line in text.split("\n"):
        s = line.strip()
        if s.startswith("#"):
            d = s[1:].strip()
            if d.startswith("if"):
                depth += 1
            elif d.startswith("endif"):
                depth = max(0, depth - 1)
            keep.append("")
            continue
        keep.append("" if depth else line)
    return "\n".join(keep)


def split_top(s, sep=","):
    parts, depth, cur = [], 0, []
    for ch in s:
        if ch in "<([{":
            depth += 1
        elif ch in ">)]}":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    if "".join(cur).strip():
        parts.append("".join(cur))
    return parts


def enum_value(expr, known):
    """A plain enum value: a number (decimal or hex), an earlier name, -x, a << b, a | b. None otherwise."""
    expr = expr.strip()
    if re.fullmatch(r"-?(0[xX][0-9a-fA-F]+|\d+)[uUlL]*", expr):
        return int(expr.rstrip("uUlL"), 0)
    if expr in known:
        return known[expr]
    for op in ("|", "<<"):
        if op in expr:
            parts = [enum_value(x, known) for x in expr.split(op)]
            if any(x is None for x in parts):
                return None
            out = parts[0]
            for x in parts[1:]:
                out = (out | x) if op == "|" else (out << x)
            return out
    if expr.startswith("(") and expr.endswith(")"):
        return enum_value(expr[1:-1], known)
    return None


def enum_range(body):
    """(lowest, highest) of an enum's values; bit-flag enums get highest = every flag set. None when unsure."""
    known, nxt, values = {}, 0, []
    for item in split_top(body):
        item = item.strip()
        if not item:
            continue
        parts = item.split("=", 1)
        name = parts[0].strip()
        if len(parts) > 1:
            v = enum_value(parts[1], known)
            if v is None:
                return None
            nxt = v
        known[name] = nxt
        values.append(nxt)
        nxt += 1
    if not values:
        return None
    lo, hi = min(values), max(values)
    if lo >= 0 and hi > len(values) and all(v == 0 or v & (v - 1) == 0 for v in values):
        hi = 0
        for v in values:
            hi |= v   # flags: any combination
    return (lo, hi)


class ClassInfo:
    def __init__(self, name, header, bases, is_struct):
        self.name, self.header, self.bases, self.is_struct = name, header, bases, is_struct
        self.methods = []          # (name, ret, params[(type, has_default, name)], is_const, is_static, decl)
        self.enums = {}           # name -> (lowest, highest) value, or None when a value is not a plain number
        self.abstract = False


def scan_header(path, rel, classes, global_enums):
    text = drop_conditionals(strip_comments(open(path, encoding="utf-8", errors="replace").read()))
    # A tiny scope walker: namespace okay at top, classes directly inside it.
    i, n = 0, len(text)
    stack = []   # entries: ("ns", name) / ("class", ClassInfo, access) / ("skip",)
    buf = []

    def in_okay_ns():
        return [e for e in stack if e[0] == "ns"] == [("ns", "okay")] and all(e[0] in ("ns", "nsend") for e in stack)

    def skip_block(j):
        depth = 0
        while j < n:
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    return j + 1
            j += 1
        return n

    while i < n:
        c = text[i]
        if c == "{":
            head = " ".join("".join(buf).split())
            buf = []
            cls = stack[-1][1] if stack and stack[-1][0] == "class" else None
            m_ns = re.fullmatch(r"(?:inline\s+)?namespace\s+([\w:]+)", head)
            m_cls = re.fullmatch(r"(?:template\s*<.*>\s*)?(class|struct)\s+(?:\[\[[^\]]*\]\]\s*)?(\w+)\s*(final)?\s*(?::\s*(.*))?", head)
            if m_ns:
                for part in m_ns.group(1).split("::"):
                    stack.append(("ns", part))
                stack.append(("nsend", len(m_ns.group(1).split("::"))))
                i += 1
                continue
            if m_cls and not head.startswith("template") and in_okay_ns() and cls is None:
                bases = []
                if m_cls.group(4):
                    for b in split_top(m_cls.group(4)):
                        b = b.strip()
                        b = re.sub(r"^(public|protected|private|virtual)\s+", "", b)
                        b = re.sub(r"^(public|protected|private|virtual)\s+", "", b)
                        bases.append(b.replace("okay::", "").strip())
                info = ClassInfo(m_cls.group(2), rel, bases, m_cls.group(1) == "struct")
                classes.setdefault(info.name, info)
                if classes[info.name] is not info:
                    info = classes[info.name]   # declared twice: keep the first
                stack.append(("class", info, "public" if m_cls.group(1) == "struct" else "private"))
                i += 1
                continue
            m_enum = re.fullmatch(r"enum\s+(?:class\s+|struct\s+)?(\w+)\s*(?::\s*[\w:\s]+)?", head)
            if m_enum:
                end = skip_block(i)
                rng = enum_range(text[i + 1:end - 1])
                if cls is not None:
                    cls.enums[m_enum.group(1)] = rng
                elif in_okay_ns():
                    global_enums[m_enum.group(1)] = rng
                i = end
                continue
            if cls is not None:
                handle_member(cls, stack[-1][2], head, True)
                i = skip_block(i)
                continue
            i = skip_block(i)
            continue
        if c == "}":
            buf = []
            if stack:
                top = stack.pop()
                if top[0] == "nsend":
                    for _ in range(top[1]):
                        stack.pop()
            i += 1
            continue
        if c == ";":
            stmt = " ".join("".join(buf).split())
            buf = []
            if stack and stack[-1][0] == "class":
                handle_member(stack[-1][1], stack[-1][2], stmt, False)
            i += 1
            continue
        if c == ":" and stack and stack[-1][0] == "class":
            word = "".join(buf).strip()
            nxt = text[i + 1] if i + 1 < n else ""
            prev = text[i - 1] if i else ""
            if word in ("public", "private", "protected") and nxt != ":" and prev != ":":
                e = stack.pop()
                stack.append(("class", e[1], word))
                buf = []
                i += 1
                continue
        buf.append(c)
        i += 1


FUNC_RE = re.compile(r"^(?P<pre>.*?)\b(?P<name>~?\w+)\s*\((?P<params>.*)\)\s*(?P<post>[^()]*)$")


def handle_member(cls, access, stmt, has_body):
    stmt = stmt.strip()
    if not stmt or "(" not in stmt:
        return
    if re.search(r"=\s*0\s*$", stmt):
        cls.abstract = True
    if access != "public":
        return
    if stmt.startswith(("template", "friend", "using", "typedef", "operator")) or "operator" in stmt.split("(")[0]:
        return
    # strip a constructor initializer list: "X() : a(1)" is a constructor anyway (no return type)
    m = FUNC_RE.match(stmt)
    if not m:
        return
    pre, name, params, post = m.group("pre").strip(), m.group("name"), m.group("params"), m.group("post").strip()
    if "<" in pre and pre.count("<") != pre.count(">"):
        return   # std::function<void(...)> member, not a function
    if name.startswith("~") or name == cls.name or not pre:
        return
    if "=" in post and ("delete" in post or "default" in post):
        return
    if post and not re.fullmatch(r"(const|noexcept|override|final|&|&&|\s)*(=\s*0)?", post):
        return
    if re.search(r"\b(virtual)\b", pre) and "override" not in post and name in SKIP_METHODS:
        return
    is_static = bool(re.search(r"\bstatic\b", pre))
    ret = re.sub(r"\b(virtual|inline|static|constexpr|explicit|\[\[nodiscard\]\]|OKAY_API)\b", "", pre)
    ret = re.sub(r"\[\[[^\]]*\]\]", "", ret).strip()
    plist = []
    if params.strip() and params.strip() != "void":
        for p in split_top(params):
            p = p.strip()
            has_def = False
            parts = split_top(p, "=")
            if len(parts) > 1:
                has_def = True
                p = parts[0].strip()
            # drop the parameter name (the last identifier, when there is a type before it)
            mm = re.match(r"^(.*?[\w>&*\]])\s+(\w+)$", p)
            named = mm and mm.group(1).strip() not in ("const", "unsigned", "signed", "long")
            ptype = mm.group(1) if named else p
            plist.append((" ".join(ptype.split()), has_def, mm.group(2) if named else ""))
    cls.methods.append((name, " ".join(ret.split()), plist, "const" in post.split(), is_static, stmt))


class Mapper:
    def __init__(self, classes, global_enums):
        self.classes, self.global_enums = classes, global_enums

    def kind(self, cls_chain, t, for_return):
        """(ArgKind, c++ conversion info) for type text `t`, or None when a script value cannot carry it."""
        t = t.replace("okay::", "").strip()
        t = re.sub(r"\bstruct\s+|\bclass\s+", "", t)
        const_ref = False
        if t.startswith("const ") and t.endswith("&"):
            t, const_ref = t[6:-1].strip(), True
        elif t.startswith("const "):
            t = t[6:].strip()
        if t.endswith("&"):
            if for_return and not t.endswith("&&"):
                t = t[:-1].strip()   # a returned reference is copied out
            else:
                return None          # a non-const reference argument is an out-parameter
        if t.endswith(" const"):
            t = t[:-6].strip()
        if t == "void":
            return ("Void", None) if for_return else None
        if t in VALUE_TYPES:
            return (VALUE_TYPES[t], t)
        if t in INT_TYPES:
            return ("Int", t)
        if t == "GameObject*":
            return ("Object", t)
        if t.endswith("*"):
            base = t[:-1].strip()
            if for_return and base in self.classes and self.is_component(base) and base not in ("Component",):
                return ("Component", base)
            return None
        # enum: nested in the class or a base, or at namespace okay; the third item is its (lowest, highest) values
        for c in cls_chain:
            if t in c.enums:
                return ("Enum", c.name + "::" + t, c.enums[t])
            if t.startswith(c.name + "::") and t.split("::", 1)[1] in c.enums:
                return ("Enum", t, c.enums[t.split("::", 1)[1]])
        if t in self.global_enums:
            return ("Enum", t, self.global_enums[t])
        if "::" in t:
            owner, inner = t.split("::", 1)
            if owner in self.classes and inner in self.classes[owner].enums:
                return ("Enum", t, self.classes[owner].enums[inner])
        return None

    def is_component(self, name, seen=None):
        if name in ("Component", "Behaviour"):   # Scene/Component.hpp: using Behaviour = Component
            return True
        seen = seen or set()
        if name in seen or name not in self.classes:
            return False
        seen.add(name)
        return any(self.is_component(re.sub(r"<.*", "", b).strip(), seen) for b in self.classes[name].bases)

    def chain(self, name):
        out, todo = [], [name]
        while todo:
            n = todo.pop(0)
            if n in self.classes and n not in ("Component", "Behaviour") and self.classes[n] not in out:
                out.append(self.classes[n])
                todo.extend(re.sub(r"<.*", "", b).strip() for b in self.classes[n].bases)
        return out


# ---- What game scripts must never reach (docs/scripting/script_bindings.md "Side effects scripts cannot reach") ----
# Before the generated layer, a script could only touch named save slots, the prefs and the net.* builtins (which have
# their own rules). So a method that reads or writes a file by name, opens a socket or a server, a window, an overlay,
# the clipboard, or a platform service is not bound. Three rules, any one is enough:
#  1. the whole class is a network, platform or file service;
#  2. a text parameter's name says it is a place (path, file, dir, folder, url, uri, address, host, link, root);
#  3. a word in the method's name says it (file, clipboard, overlay, cloud, export, import, server, client, relay,
#     socket, connect, url, download, upload, http, workshop, lobby), or it is on the list below (a method that
#     writes a file it was given earlier, through a property).
DENY_CLASSES = {
    "NetworkManager", "NetworkObject", "NetworkSync", "NetworkPlayerSpawner", "NetValidator",   # sockets: net.* builtins
    "SteamManager",      # platform service (overlay, cloud files, lobbies)
    "UIFilePicker",      # browses the disk
    "UIHotReload",       # reads files from disk as they change
}
DENY_METHODS = {
    "NavMeshSurface.Save", "BuilderMode.SaveModel", "BuilderMode.SaveMap", "SurvivalSave.Save", "SurvivalSave.Load",
    "Terrain.ExportHeightmap", "Terrain.ImportHeightmap", "VehicleController.ToFile", "VehicleController.LoadPresetText",
    "VehicleController.SavePresetText", "ScriptComponent.SetPath", "VisualScriptComponent.SetPath",
}
DENY_PARAM_WORDS = {"path", "paths", "file", "files", "filename", "filepath", "dir", "dirs", "directory", "folder",
                    "url", "uri", "address", "host", "hostname", "link", "root"}
DENY_NAME_WORDS = {"file", "files", "clipboard", "overlay", "cloud", "export", "import", "server", "client", "relay",
                   "socket", "connect", "url", "download", "upload", "http", "workshop", "lobby"}


def words(name):
    """The lower-case words of a name: filePath -> file, path; ExportHeightmap -> export, heightmap."""
    return [w.lower() for w in re.findall(r"[A-Z]+(?![a-z])|[A-Z]?[a-z]+|\d+", name)]


def denied(cls_name, owner_name, mname, params):
    """Why a method is not bound (a short reason), or ""."""
    if cls_name in DENY_CLASSES or owner_name in DENY_CLASSES:
        return "class"
    if f"{cls_name}.{mname}" in DENY_METHODS or f"{owner_name}.{mname}" in DENY_METHODS:
        return "list"
    for (ptype, _, pname) in params:
        if ("string" in ptype or "char" in ptype) and set(words(pname)) & DENY_PARAM_WORDS:
            return "parameter " + pname   # text that names a place (a direction `dir` as a vector is fine)
    if set(words(mname)) & DENY_NAME_WORDS:
        return "name"
    return ""


# Count- and index-like whole-number parameters: a script value above this is an error, so one call cannot make the
# engine allocate gigabytes (LineRenderer.SetPositionCount(2e9)).
COUNT_WORDS = {"count", "size", "capacity", "index", "idx", "length", "len", "num", "number", "slot", "cells",
               "resolution", "segments", "points", "max", "rows", "cols", "columns", "width", "height", "n", "i"}
MAX_COUNT = 1 << 20


def count_like(pname):
    return bool(set(words(pname)) & COUNT_WORDS)


def load_excludes():
    out = set()
    if os.path.exists(EXCLUDE_FILE):
        for line in open(EXCLUDE_FILE, encoding="utf-8"):
            line = line.split("#", 1)[0].strip()
            if line:
                out.add(line)
    return out


def collect():
    classes, global_enums = {}, {}
    headers = []
    for dirpath, _, files in os.walk(os.path.join(INCLUDE, "okay")):
        for f in sorted(files):
            if f.endswith(".hpp"):
                full = os.path.join(dirpath, f)
                headers.append((full, os.path.relpath(full, INCLUDE).replace(os.sep, "/")))
    headers.sort(key=lambda h: h[1])
    for full, rel in headers:
        scan_header(full, rel, classes, global_enums)
    return classes, global_enums


def build_model():
    classes, global_enums = collect()
    mapper = Mapper(classes, global_enums)
    excludes = load_excludes()
    comps = sorted(n for n in classes if n != "Component" and mapper.is_component(n) and n not in excludes)
    model, stats = [], {"classes": len(comps), "public_methods": 0, "bound": 0, "skipped_type": 0, "skipped_other": 0,
                        "excluded": 0, "denied": 0}
    for name in comps:
        chain = mapper.chain(name)
        seen_sigs, methods = set(), []
        for owner in chain:
            for (mname, ret, params, is_const, is_static, decl) in owner.methods:
                stats["public_methods"] += 1 if owner is chain[0] else 0
                if mname in SKIP_METHODS or is_static:
                    stats["skipped_other"] += 1 if owner is chain[0] else 0
                    continue
                if f"{name}.{mname}" in excludes or f"{owner.name}.{mname}" in excludes:
                    stats["excluded"] += 1 if owner is chain[0] else 0
                    continue
                if denied(name, owner.name, mname, params):
                    stats["denied"] += 1 if owner is chain[0] else 0
                    continue
                rk = mapper.kind(chain, ret, True)
                pks = [mapper.kind(chain, p, False) for (p, _, _) in params]
                if rk is None or any(k is None for k in pks):
                    stats["skipped_type"] += 1 if owner is chain[0] else 0
                    continue
                sig = (mname, tuple(k[0] for k in pks))
                if sig in seen_sigs:
                    continue   # overridden in a subclass: the subclass's one is already in
                seen_sigs.add(sig)
                required = sum(1 for (_, d, _) in params if not d)
                methods.append({"name": mname, "ret": rk, "params": pks, "required": required, "owner": owner.name,
                                "const": is_const, "pnames": [pn for (_, _, pn) in params]})
                stats["bound"] += 1 if owner is chain[0] else 0
        model.append({"name": name, "header": classes[name].header, "methods": methods,
                      "abstract": classes[name].abstract})
    return model, stats, classes


def conv_arg(k, idx, pname=""):
    kind, ctype = k[0], k[1]
    a = f"a[{idx}]"
    if kind == "Bool": return f"{a}.AsBool()"
    if kind == "Int" and count_like(pname): return f"static_cast<{ctype}>(sb::ArgCount({a}, {idx}, {MAX_COUNT}))"
    if kind == "Int" and ctype in ("char", "std::uint8_t", "uint8_t", "std::int8_t", "int8_t", "short", "std::int16_t",
                                   "int16_t", "std::uint16_t", "uint16_t", "int", "std::int32_t", "int32_t"):
        return f"static_cast<{ctype}>(sb::ArgIntIn({a}, {idx}, std::numeric_limits<{ctype}>::min(), std::numeric_limits<{ctype}>::max()))"
    if kind == "Int": return f"static_cast<{ctype}>(sb::ArgInt({a}))"
    if kind == "Float": return f"static_cast<{ctype}>({a}.AsFloat())"
    if kind == "String": return f"{a}.AsString()"
    if kind == "Vec2": return f"sb::ArgVec2({a})"
    if kind == "Vec3": return f"{a}.AsVec3()"
    if kind == "Vec4": return f"sb::ArgVec4({a})"
    if kind == "Quat": return f"sb::ArgQuat({a})"
    if kind == "Color": return f"sb::ArgColor({a})"
    if kind == "Enum":
        rng = k[2] if len(k) > 2 else None
        if rng is None:   # values we could not read: at least the range of an int
            return f"static_cast<{ctype}>(sb::ArgIntIn({a}, {idx}, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()))"
        return f"static_cast<{ctype}>(sb::ArgIntIn({a}, {idx}, {rng[0]}LL, {rng[1]}LL))"
    if kind == "Object": return f"sb::ArgObject({a})"
    raise ValueError(kind)


def wrap_ret(kind, ctype, expr):
    if kind == "Void": return f"{expr}; return V{{}};"
    if kind == "Enum": return f"return V{{static_cast<float>(static_cast<long long>({expr}))}};"
    if kind == "Int": return f"return V{{static_cast<float>({expr})}};"
    if kind == "Float": return f"return V{{static_cast<float>({expr})}};"
    if kind in ("Bool", "String", "Vec3"): return f"return V{{{expr}}};"
    if kind == "Vec2": return f"return sb::RetVec2({expr});"
    if kind == "Vec4": return f"return sb::RetVec4({expr});"
    if kind == "Quat": return f"return sb::RetQuat({expr});"
    if kind == "Color": return f"return sb::RetColor({expr});"
    if kind == "Object": return f"return sb::RetObject({expr});"
    if kind == "Component": return f"return sb::RetComponent({expr});"
    raise ValueError(kind)


def render(model):
    headers = sorted({c["header"] for c in model})
    L = ["// GENERATED by scripts/gen_script_bindings.py from the engine's own headers. Do not edit by hand:",
         "// run `python3 scripts/gen_script_bindings.py` (a test checks it with --check). One entry per component class",
         "// and per public method whose types a script value can carry; properties come from the PropertyRegistry.",
         '#include "okay/Scripting/ScriptBindings.hpp"', "#include <limits>"]
    L += [f'#include "{h}"' for h in headers]
    L += ["", "namespace okay::scriptbind {", "namespace {", "using V = vs::VsValue;", "namespace sb = okay::scriptbind;", ""]
    for c in model:
        cname = c["name"]
        L.append(f"// {c['header']}")
        L.append(f"void Bind{cname}() {{")
        L.append(f'    ClassBinding c = MakeClass<{cname}>("{cname}", nullptr, 0);')
        if c["methods"]:
            L.append("    c.methods = {")
            for m in c["methods"]:
                kinds = [k[0] for k in m["params"]]
                n = len(kinds)
                body = [f"auto& s = static_cast<{cname}&>(self);"]
                for argc in range(n, m["required"] - 1, -1):
                    args = ", ".join(conv_arg(k, i, m["pnames"][i]) for i, k in enumerate(m["params"][:argc]))
                    stmt = wrap_ret(m["ret"][0], m["ret"][1], f"s.{m['name']}({args})")
                    body.append(stmt if argc == m["required"] else f"if (n >= {argc}) {{ {stmt} }}")
                pk = "{" + ", ".join(f"ArgKind::{k}" for k in kinds) + "}"
                unused = "(void)a; (void)n; " if n == 0 else ("(void)n; " if n == m["required"] else "")
                L.append(f'        {{"{m["name"]}", {pk}, {m["required"]}, ArgKind::{m["ret"][0]}, '
                         f'[](Component& self, const V* a, int n) -> V {{ {unused}{" ".join(body)} }}, {"true" if m["const"] else "false"}}},')
            L.append("    };")
        L.append("    AddClass(std::move(c));")
        L.append("}")
    L.append("")
    L.append("} // namespace")
    L.append("")
    # The DENY_* rules as data, so Lua's and the typed-language table apply the same rules (ScriptBindings.hpp).
    def strs(items):
        return "{" + ", ".join(f'"{x}"' for x in sorted(items)) + "}"
    L.append("const DenyRules& GeneratedDenyRules() {")
    L.append("    static const DenyRules r{")
    L.append(f"        {strs(DENY_CLASSES)},")
    L.append(f"        {strs(DENY_METHODS)},")
    L.append(f"        {strs(DENY_PARAM_WORDS)},")
    L.append(f"        {strs(DENY_NAME_WORDS)}}};")
    L.append("    return r;")
    L.append("}")
    L.append("")
    L.append("void RegisterGeneratedBindings() {")
    for c in model:
        L.append(f"    Bind{c['name']}();")
    L.append("}")
    L.append("")
    L.append("} // namespace okay::scriptbind")
    L.append("")
    return "\n".join(L)


def audit(model, stats, classes):
    print(f"component classes found in engine/include/okay: {stats['classes']}")
    print(f"their own public methods: {stats['public_methods']}")
    print(f"  bound (types a script value carries): {stats['bound']}")
    print(f"  skipped, a type a script value cannot carry (pointers, references, containers, callbacks): {stats['skipped_type']}")
    print(f"  skipped, engine callbacks or static: {stats['skipped_other']}")
    print(f"  excluded by scripts/script_bindings_exclude.txt: {stats['excluded']}")
    print(f"  denied (files, network, platform, clipboard: DENY_* in this script): {stats['denied']}")
    total = sum(len(c["methods"]) for c in model)
    print(f"bound methods including inherited ones: {total}")
    # What the hand-written builtins reached before this layer (an upper bound: a class counts when a builtin names
    # it with GetComponent<...> / AddComponent<...>, a method when a builtin calls ->Name( or .Name( anywhere).
    src_dir = os.path.join(ROOT, "engine", "src", "Scripting")
    hand = ""
    for f in sorted(os.listdir(src_dir)):
        if f.startswith(("OkayScriptVM", "ScriptApi")) and f.endswith((".cpp", ".hpp")):
            hand += open(os.path.join(src_dir, f), encoding="utf-8", errors="replace").read()
    reached = [c for c in model if re.search(r"(Get|Add|GetOrAdd)Component<\s*" + c["name"] + r"\s*>", hand)]
    own = [m for c in reached for m in c["methods"] if m["owner"] == c["name"]]
    called = [m for m in own if re.search(r"(->|\.)" + m["name"] + r"\(", hand)]
    print(f"before (hand-written builtins only): {len(reached)} of {len(model)} classes named, "
          f"at most {len(called)} of {stats['bound']} bindable methods called")


def main():
    model, stats, classes = build_model()
    if "--audit" in sys.argv:
        audit(model, stats, classes)
        return 0
    text = render(model)
    if "--check" in sys.argv:
        cur = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        if cur != text:
            print("engine/src/Scripting/ScriptBindingsGenerated.cpp is out of date: run python3 scripts/gen_script_bindings.py")
            return 1
        print("script bindings up to date")
        return 0
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"wrote {os.path.relpath(OUT, ROOT)}: {len(model)} classes, {sum(len(c['methods']) for c in model)} methods")
    return 0


if __name__ == "__main__":
    sys.exit(main())
