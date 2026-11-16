#!/usr/bin/env python3
"""Measure where every visual-script node sits in the Add Node menu, and write the category docs from it.

The data only exists in C++ (the node registry, the engine-API table and the Actions steps), so a test dumps it:

    <build>/tests/test_vs_categories --dump nodes.tsv

then this script (standard library only) reads that file:

    python3 scripts/vs_category_report.py nodes.tsv                 # the tables for the places the menu uses now
    python3 scripts/vs_category_report.py nodes.tsv --legacy        # the same tables for the OLD places (before the split)
    python3 scripts/vs_category_report.py nodes.tsv --moved         # every node that moved: old place -> new place
    python3 scripts/vs_category_report.py nodes.tsv --tree          # the topic tree with counts
    python3 scripts/vs_category_report.py nodes.tsv --markdown-audit --update docs/visual_script_category_audit.md
    python3 scripts/vs_category_report.py nodes.tsv --markdown-topics > docs/visual_scripting_topics.md

The dump holds the old place of every row next to the new one (the old places are frozen in
editor/VisualScriptLegacyPlaces.hpp), so one dump is enough for the before and after tables. The audit page keeps its
hand-written parts and only the block between its two "generated" markers is rewritten.

A row is one palette entry: a node, an engine-API node, or an Actions step. A "listed" row is one the menu shows
(duplicates and the nodes waiting to be tested are not listed). "Browse" rows are the listed ones that also show while
browsing a topic (a step that is the same job as a node is found by search only).

Columns of the dump (tab separated, lines starting with # are the topic tables):
  kind id title category subcategory browse level oldcat api hidden group help was_category was_subcategory
"""
import argparse
import collections
import csv
import re
import sys

# --------------------------------------------------------------------------------------------------------------------
# Reading the dump

class Row:
    __slots__ = ("kind", "id", "title", "cat", "sub", "browse", "level", "oldcat", "api", "hidden", "group", "help",
                 "was_cat", "was_sub")

    def __init__(self, f):
        f = f + [""] * (14 - len(f))
        self.kind, self.id, self.title, self.cat, self.sub = f[0], f[1], f[2], f[3], f[4]
        self.browse = f[5] == "1"
        self.level, self.oldcat, self.api, self.hidden, self.group, self.help = f[6], f[7], f[8], f[9], f[10], f[11]
        # A dump from the old code has no old places: its places ARE the old places.
        self.was_cat = f[12] or self.cat
        self.was_sub = f[13] if f[12] else self.sub

    @property
    def listed(self):
        return self.hidden == ""

    def place(self, legacy):
        return (self.was_cat, self.was_sub) if legacy else (self.cat, self.sub)


class Dump:
    def __init__(self, path):
        self.rows = []
        self.cats = []      # (name, section, blurb), in menu order
        self.subs = []      # (category, name, blurb)
        self.legacy_cats = []  # (name, blurb): the old topics
        with open(path, encoding="utf-8", newline="") as fh:
            for line in fh.read().split("\n"):
                if not line:
                    continue
                f = line.split("\t")
                if line.startswith("#cat"):
                    self.cats.append((f[1], f[2], f[3] if len(f) > 3 else ""))
                elif line.startswith("#sub"):
                    self.subs.append((f[1], f[2], f[3] if len(f) > 3 else ""))
                elif line.startswith("#legacycat"):
                    self.legacy_cats.append((f[1], f[2] if len(f) > 2 else ""))
                elif f[0] == "kind":
                    continue
                else:
                    self.rows.append(Row(f))
        self.has_old_places = any(r.was_cat != r.cat or r.was_sub != r.sub for r in self.rows)


def browse_rows(d):
    return [r for r in d.rows if r.listed and r.browse]


# --------------------------------------------------------------------------------------------------------------------
# Tables

def counts(rows, legacy):
    by_cat = collections.Counter()
    by_sub = collections.Counter()
    direct = collections.Counter()
    for r in rows:
        c, s = r.place(legacy)
        by_cat[c] += 1
        if s:
            by_sub[(c, s)] += 1
        else:
            direct[c] += 1
    return by_cat, by_sub, direct


def fmt_place(c, s):
    return f"{c} / {s}" if s else c


def tables(d, legacy, out):
    rows = browse_rows(d)
    by_cat, by_sub, direct = counts(rows, legacy)
    kinds = collections.Counter(r.kind for r in d.rows)
    listed = [r for r in d.rows if r.listed]
    out.append(f"palette rows: {len(d.rows)} ({', '.join(f'{k} {v}' for k, v in sorted(kinds.items()))})")
    out.append(f"listed in the menu: {len(listed)}; shown while browsing: {len(rows)}")
    out.append(f"topics in use: {len(by_cat)}; subcategories in use: {len(by_sub)}")
    out.append("")
    out.append("nodes per topic (direct = not in a subcategory):")
    for c, n in sorted(by_cat.items(), key=lambda kv: -kv[1]):
        subs = sorted(((s, v) for (cc, s), v in by_sub.items() if cc == c), key=lambda kv: -kv[1])
        out.append(f"  {c:30s} {n:5d}  direct {direct[c]:4d}" + (f"  subs: " + ", ".join(f"{s} {v}" for s, v in subs) if subs else ""))
    out.append("")
    buckets = collections.Counter()
    for r in rows:
        buckets[r.place(legacy)] += 1
    out.append("the 15 biggest buckets (topic / subcategory):")
    for (c, s), n in buckets.most_common(15):
        out.append(f"  {n:5d}  {fmt_place(c, s)}")


# --------------------------------------------------------------------------------------------------------------------
# Suspects: a node whose name or help sounds like another topic than the one it sits in

# topic key -> words (matched as whole words in the id, title and help). These are the topics a person searches for.
HINTS = collections.OrderedDict([
    ("weather", ["weather", "fog", "rain", "snow", "wind"]),
    ("story", ["quest", "quests", "objective", "objectives", "dialogue", "conversation", "story"]),
    ("navigation", ["nav", "navigation", "path", "pathfinding", "waypoint", "walkable", "grid"]),
    ("behavior trees", ["behavior", "blackboard", "bt"]),
    ("inventory", ["inventory", "item", "items", "equip", "equipment", "craft", "crafting", "loot", "slot", "carry"]),
    ("health", ["health", "damage", "heal", "armor", "invincible", "lives", "respawn", "survival"]),
    ("weapons", ["weapon", "ammo", "shoot", "reload", "projectile"]),
    ("online", ["net", "network", "lobby", "steam", "rpc", "multiplayer", "server", "peer", "relay", "playfab",
                "matchmaking", "leaderboard", "account", "cloud", "achievement"]),
    ("audio", ["sound", "music", "audio", "volume", "pitch", "mute"]),
    ("camera", ["camera", "fov"]),
    ("ui", ["screen", "slider", "toast", "tooltip", "modal", "tab", "tabs", "dropdown", "ui"]),
    ("saving", ["save", "saved", "load", "slot", "checkpoint", "prefs", "autosave"]),
    ("tween", ["tween", "ease", "easing"]),
    ("input", ["key", "keys", "mouse", "gamepad", "touch", "gesture", "axis", "swipe", "pinch"]),
    ("physics", ["force", "impulse", "velocity", "gravity", "raycast", "overlap", "collision", "trigger", "rigidbody"]),
    ("animation", ["anim", "animation", "clip", "ragdoll", "blend"]),
    ("ik", ["ik"]),
    ("building", ["build"]),
    ("particles", ["particle", "particles", "emit", "trail"]),
    ("spline", ["spline"]),
    ("text", ["text", "string", "upper", "lower", "trim", "substring"]),
    ("lists", ["list", "array", "map", "struct", "json"]),
    ("localization", ["language", "localize", "translation"]),
    ("display", ["display", "monitor", "resolution", "refresh"]),
    ("cutscene", ["sequence", "cutscene", "timeline"]),
])


def words(text):
    return set(re.findall(r"[a-z0-9]+", re.sub(r"([a-z])([A-Z])", r"\1 \2", text).lower()))


def node_hints(r):
    """Topics this row talks about: the id and the title count first, the help sentence counts only as a tie breaker."""
    primary = words(r.id.replace("Fn.", "") + " " + r.title)
    found = []
    for key, ws in HINTS.items():
        if primary & set(ws):
            found.append(key)
    return found


def home_words(d, cat, sub, legacy):
    """Words a topic's own name, subcategory and blurb contain (so a node about them is 'mentioned')."""
    blurb = ""
    pool = d.legacy_cats if legacy else [(c, b) for c, _, b in d.cats]
    for c, b in pool:
        if c == cat:
            blurb = b
    sub_blurb = ""
    if not legacy:
        for c, s, b in d.subs:
            if c == cat and s == sub:
                sub_blurb = b
    return words(cat + " " + sub + " " + blurb + " " + sub_blurb)


def stem(w):
    """A very light stemmer: splines -> spline, quests -> quest, building -> build. Enough to compare words."""
    if w.endswith("ing") and len(w) > 5:
        return w[:-3]
    if w.endswith("s") and not w.endswith("ss") and len(w) > 3:
        return w[:-1]
    return w


def suspects(d, legacy, out, limit=12):
    rows = browse_rows(d)
    flagged = collections.defaultdict(list)   # (place, hint) -> rows
    for r in rows:
        hints = node_hints(r)
        if not hints:
            continue
        c, s = r.place(legacy)
        home = {stem(w) for w in home_words(d, c, s, legacy)}
        # the node is at home when ANY of its hinted topics is named by the place
        at_home = False
        for h in hints:
            if {stem(w) for w in HINTS[h]} & home:
                at_home = True
                break
        if not at_home:
            flagged[(c, hints[0])].append(r)
    out.append("nodes whose name sounds like another topic than the one they sit in (a heuristic: id and title words):")
    total = sum(len(v) for v in flagged.values())
    out.append(f"  {total} rows flagged")
    for (c, h), rs in sorted(flagged.items(), key=lambda kv: -len(kv[1]))[:limit]:
        sample = ", ".join(x.title or x.id for x in rs[:6])
        out.append(f"  {len(rs):4d}  in {c}  about {h}:  {sample}{' ...' if len(rs) > 6 else ''}")
    return flagged


def blurb_gaps(d, legacy, out):
    """Subcategories (and a count of direct nodes) the topic's own blurb never mentions."""
    rows = browse_rows(d)
    by_place = collections.Counter(r.place(legacy) for r in rows)
    out.append("subcategories that the blurb of their topic does not mention:")
    pool = {c: b for c, b in (d.legacy_cats if legacy else [(c, b) for c, _, b in d.cats])}
    gaps = []
    for (c, s), n in sorted(by_place.items(), key=lambda kv: -kv[1]):
        if not s:
            continue
        bw = {stem(w) for w in words(c + " " + pool.get(c, ""))}
        if not ({stem(w) for w in words(s)} & bw):
            gaps.append((c, s, n))
    for c, s, n in gaps:
        out.append(f"  {n:4d}  {c} / {s}")
    out.append(f"  {sum(n for _, _, n in gaps)} rows in {len(gaps)} subcategories")


def duplicates(d, legacy, out, limit=25):
    """The same job under different names in different places (listed rows only)."""
    rows = [r for r in d.rows if r.listed and r.browse]
    def norm_title(t):
        t = re.sub(r"\([^)]*\)", "", t.lower())
        t = re.sub(r"\b(the|a|an|object|this)\b", "", t)
        return re.sub(r"[^a-z0-9]", "", t)
    def norm_id(i):
        i = i.replace("Fn.", "")
        return re.sub(r"[^a-z0-9]", "", i.lower())
    groups = collections.defaultdict(list)
    for r in rows:
        groups[("title", norm_title(r.title))].append(r)
        groups[("id", norm_id(r.id))].append(r)
    seen = set()
    found = []
    for (kind, key), rs in groups.items():
        if len(rs) < 2 or not key:
            continue
        ids = tuple(sorted({(r.kind, r.id) for r in rs}))
        if len(ids) < 2 or ids in seen:
            continue
        places = {r.place(legacy) for r in rs}
        if len(places) < 2:
            continue
        seen.add(ids)
        found.append((kind, key, rs))
    out.append("the same name or the same job in more than one place (listed rows; title or id match):")
    out.append(f"  {len(found)} groups")
    for kind, key, rs in sorted(found, key=lambda x: (-len({r.place(legacy) for r in x[2]}), x[1]))[:limit]:
        where = "; ".join(f"{r.title} [{r.kind}:{r.id}] in {fmt_place(*r.place(legacy))}" for r in rs[:4])
        out.append(f"  {key}: {where}{' ...' if len(rs) > 4 else ''}")
    return found


def moved_rows(d):
    return [r for r in d.rows if (r.was_cat, r.was_sub) != (r.cat, r.sub)]


# --------------------------------------------------------------------------------------------------------------------
# Markdown outputs

def md_escape(s):
    return s.replace("|", "\\|")


def md_counts_table(d, legacy, lines):
    rows = browse_rows(d)
    by_cat, by_sub, direct = counts(rows, legacy)
    lines.append("| Topic | Nodes | Directly in the topic | Subcategories (nodes) |")
    lines.append("|---|---:|---:|---|")
    order = [c for c, _, _ in d.cats] if not legacy else [c for c, _ in d.legacy_cats]
    for c in order + [c for c in by_cat if c not in order]:
        if c not in by_cat:
            continue
        subs = sorted(((s, v) for (cc, s), v in by_sub.items() if cc == c), key=lambda kv: -kv[1])
        lines.append(f"| {md_escape(c)} | {by_cat[c]} | {direct[c]} | " + md_escape(", ".join(f"{s} ({v})" for s, v in subs)) + " |")


GEN_START = "<!-- generated:start -->"
GEN_END = "<!-- generated:end -->"


def flagged_table(d, legacy, lines, limit=12):
    rows = browse_rows(d)
    flagged = collections.defaultdict(list)
    for r in rows:
        hints = node_hints(r)
        if not hints:
            continue
        c, s = r.place(legacy)
        home = {stem(w) for w in home_words(d, c, s, legacy)}
        if not any({stem(w) for w in HINTS[h]} & home for h in hints):
            flagged[(c, hints[0])].append(r)
    total = sum(len(v) for v in flagged.values())
    lines.append(f"{total} rows. The biggest groups:")
    lines.append("")
    lines.append("| Rows | Sat in | The name talks about | Examples |")
    lines.append("|---:|---|---|---|")
    for (c, h), rs in sorted(flagged.items(), key=lambda kv: -len(kv[1]))[:limit]:
        sample = ", ".join(x.title or x.id for x in rs[:5]) + (" ..." if len(rs) > 5 else "")
        lines.append(f"| {len(rs)} | {md_escape(c)} | {h} | {md_escape(sample)} |")
    return total


def blurb_gap_lines(d, legacy, lines, limit=14):
    rows = browse_rows(d)
    by_place = collections.Counter(r.place(legacy) for r in rows)
    pool = {c: b for c, b in (d.legacy_cats if legacy else [(c, b) for c, _, b in d.cats])}
    gaps = []
    for (c, s), n in sorted(by_place.items(), key=lambda kv: -kv[1]):
        if not s:
            continue
        bw = {stem(w) for w in words(c + " " + pool.get(c, ""))}
        if not ({stem(w) for w in words(s)} & bw):
            gaps.append((c, s, n))
    lines.append(f"{sum(n for _, _, n in gaps)} rows sit in {len(gaps)} subcategories that the one-line description of their topic does not mention"
                 + (":" if gaps else "."))
    if gaps:
        lines.append("")
        lines.append("| Rows | Subcategory |")
        lines.append("|---:|---|")
        for c, s, n in gaps[:limit]:
            lines.append(f"| {n} | {md_escape(c)} / {md_escape(s)} |")
        if len(gaps) > limit:
            lines.append(f"| | and {len(gaps) - limit} more |")


def duplicate_lines(d, legacy, lines, limit=14):
    out = []
    found = duplicates(d, legacy, out, limit=limit)
    lines.append(f"{len(found)} groups of rows have the same name, or the same job under another name, and sit in different places:")
    lines.append("")
    lines.append("| Name | The rows | Where |")
    lines.append("|---|---|---|")
    for kind, key, rs in sorted(found, key=lambda x: (-len({r.place(legacy) for r in x[2]}), x[1]))[:limit]:
        rows_txt = "; ".join(f"{r.title or r.id} ({r.kind})" for r in rs[:3])
        where = "; ".join(sorted({fmt_place(*r.place(legacy)) for r in rs}))
        lines.append(f"| {md_escape(key)} | {md_escape(rows_txt)} | {md_escape(where)} |")


def markdown_audit(d, out):
    """The generated block of docs/visual_script_category_audit.md. d has the old places in it (was_*)."""
    lines = out
    if not d.has_old_places:
        lines.append("(This dump has no old places, so there is nothing to compare.)")
        return
    new_rows = browse_rows(d)
    old_cat, old_sub, old_direct = counts(new_rows, True)
    new_cat, new_sub, new_direct = counts(new_rows, False)
    moved = [r for r in new_rows if (r.was_cat, r.was_sub) != (r.cat, r.sub)]
    kinds = collections.Counter(r.kind for r in d.rows)
    listed = [r for r in d.rows if r.listed]
    lines.append("### Numbers at a glance")
    lines.append("")
    lines.append(f"There are {len(d.rows)} palette rows ({', '.join(f'{v} {k}' for k, v in sorted(kinds.items()))}). {len(listed)} are listed in the menu;")
    lines.append(f"{len(new_rows)} of those also show while you click through a topic. All the counts below are those {len(new_rows)} rows.")
    lines.append("")
    lines.append("| | Before | After |")
    lines.append("|---|---:|---:|")
    lines.append(f"| Topics | {len(old_cat)} | {len(new_cat)} |")
    lines.append(f"| Subcategories | {len(old_sub)} | {len(new_sub)} |")
    lines.append(f"| Sections (headings in the sidebar) | 0 | {len({sec for _, sec, _ in d.cats})} |")
    lines.append(f"| Most nodes directly in one topic | {max(old_direct.values())} ({max(old_direct, key=old_direct.get)}) | {max(new_direct.values())} ({max(new_direct, key=new_direct.get)}) |")
    lines.append(f"| Largest single bucket (topic or subcategory) | {max(counts_bucket(new_rows, True).values())} | {max(counts_bucket(new_rows, False).values())} |")
    lines.append(f"| Topics holding more than 30 nodes directly | {sum(1 for v in old_direct.values() if v > 30)} | {sum(1 for v in new_direct.values() if v > 30)} |")
    lines.append(f"| Rows whose name sounds like another topic (heuristic) | {flagged_count(d, True)} | {flagged_count(d, False)} |")
    lines.append(f"| Rows in a subcategory their topic's description never mentions | {blurb_gap_rows(d, True)} | {blurb_gap_rows(d, False)} |")
    lines.append(f"| Rows that changed place | | {len(moved)} of {len(new_rows)} |")
    lines.append("")
    lines.append("### Before: the old 19 topics")
    lines.append("")
    md = []
    md_counts_table(d, True, md)
    lines.extend(md)
    lines.append("")
    lines.append("The 15 biggest buckets:")
    lines.append("")
    lines.append("| Nodes | Bucket |")
    lines.append("|---:|---|")
    for (c, sname), n in collections.Counter(r.place(True) for r in new_rows).most_common(15):
        lines.append(f"| {n} | {md_escape(fmt_place(c, sname))} |")
    lines.append("")
    lines.append("Nodes whose name or description sounds like another topic than the one they sat in (a heuristic: whole words in the id and the title):")
    lines.append("")
    flagged_table(d, True, lines)
    lines.append("")
    blurb_gap_lines(d, True, lines)
    lines.append("")
    duplicate_lines(d, True, lines)
    lines.append("")
    lines.append("### After: the new topics")
    lines.append("")
    md = []
    md_counts_table(d, False, md)
    lines.extend(md)
    lines.append("")
    lines.append("The 15 biggest buckets now:")
    lines.append("")
    lines.append("| Nodes | Bucket |")
    lines.append("|---:|---|")
    for (c, sname), n in collections.Counter(r.place(False) for r in new_rows).most_common(15):
        lines.append(f"| {n} | {md_escape(fmt_place(c, sname))} |")
    lines.append("")
    lines.append("Same heuristic, new places:")
    lines.append("")
    flagged_table(d, False, lines)
    lines.append("")
    blurb_gap_lines(d, False, lines)
    lines.append("")
    duplicate_lines(d, False, lines)
    lines.append("")
    # ---- moved
    lines.append("### Every node that moved")
    lines.append("")
    lines.append(f"{len(moved)} of {len(new_rows)} rows changed place; {len(new_rows) - len(moved)} kept the same topic name and subcategory (for example Debug).")
    lines.append("")
    lines.append("#### By old place")
    lines.append("")
    lines.append("| Old place | Rows | Went to (rows) |")
    lines.append("|---|---:|---|")
    by_old = collections.defaultdict(collections.Counter)
    for r in moved:
        by_old[(r.was_cat, r.was_sub)][(r.cat, r.sub)] += 1
    for (c, sname), dest in sorted(by_old.items(), key=lambda kv: -sum(kv[1].values())):
        total = sum(dest.values())
        where = ", ".join(f"{fmt_place(dc, ds)} ({n})" for (dc, ds), n in dest.most_common(8))
        if len(dest) > 8:
            where += f", and {len(dest) - 8} more"
        lines.append(f"| {md_escape(fmt_place(c, sname))} | {total} | {md_escape(where)} |")
    lines.append("")
    lines.append("#### The full list")
    lines.append("")
    lines.append("Sorted by the new place. Kind is node (hand-written), library (`Lb`), gameplay (`Gl`), builtin (a text-script function),")
    lines.append("api (an engine-API property or method), step (an Actions step) or check (an Actions condition). The Id is what a graph file")
    lines.append("stores (a builtin starts with `Fn.`), or the step's short name.")
    lines.append("")
    cur = None
    for r in sorted(moved, key=lambda r: (topic_index(d, r.cat), sub_index(d, r.cat, r.sub), r.title.lower(), r.id)):
        place = fmt_place(r.cat, r.sub)
        if place != cur:
            cur = place
            lines.append("")
            lines.append(f"##### {place}")
            lines.append("")
            lines.append("| Node | Kind | Id | Was |")
            lines.append("|---|---|---|---|")
        lines.append(f"| {md_escape(r.title)} | {r.kind} | `{md_escape(r.id)}` | {md_escape(fmt_place(r.was_cat, r.was_sub))} |")


def counts_bucket(rows, legacy):
    return collections.Counter(r.place(legacy) for r in rows)


def flagged_count(d, legacy):
    n = 0
    for r in browse_rows(d):
        hints = node_hints(r)
        if not hints:
            continue
        c, s = r.place(legacy)
        home = {stem(w) for w in home_words(d, c, s, legacy)}
        if not any({stem(w) for w in HINTS[h]} & home for h in hints):
            n += 1
    return n


def blurb_gap_rows(d, legacy):
    rows = browse_rows(d)
    by_place = collections.Counter(r.place(legacy) for r in rows)
    pool = {c: b for c, b in (d.legacy_cats if legacy else [(c, b) for c, _, b in d.cats])}
    total = 0
    for (c, sname), n in by_place.items():
        if not sname:
            continue
        bw = {stem(w) for w in words(c + " " + pool.get(c, ""))}
        if not ({stem(w) for w in words(sname)} & bw):
            total += n
    return total


def update_block(path, text):
    """Replace what stands between the two generated markers of a page and keep the rest."""
    with open(path, encoding="utf-8") as fh:
        page = fh.read()
    i, j = page.find(GEN_START), page.find(GEN_END)
    if i < 0 or j < i:
        raise SystemExit(f"{path}: the page has no {GEN_START} ... {GEN_END} block")
    page = page[: i + len(GEN_START)] + "\n\n" + text.rstrip("\n") + "\n\n" + page[j:]
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(page)


def topic_index(d, cat):
    for i, (c, _, _) in enumerate(d.cats):
        if c == cat:
            return i
    return 999


def sub_index(d, cat, sub):
    if not sub:
        return -1
    for i, (c, s, _) in enumerate(d.subs):
        if c == cat and s == sub:
            return i
    return 999


def markdown_topics(d, out):
    lines = out
    lines.append("# Where to find each visual-script node")
    lines.append("")
    lines.append("Every node in the Add Node menu, by the topic it sits in. Generated by `scripts/vs_category_report.py` from")
    lines.append("the dump of `tests/test_vs_categories.cpp`; do not edit by hand. The rules (and how to place a new node) are in")
    lines.append("[visual_scripting_naming.md](visual_scripting_naming.md). Search finds a node by its name, by its topic, and by")
    lines.append("the old topic it used to sit in ([visual_script_category_audit.md](visual_script_category_audit.md)).")
    lines.append("")
    lines.append("The name on the node comes first. A word in backticks is the type stored in a `.okayvs` file, or for an Actions step")
    lines.append("its short name. **Basic**, **Intermediate** and **Advanced** are the level badges in the menu; a node with no mark is")
    lines.append("Intermediate.")
    lines.append("")
    rows = browse_rows(d)
    by_place = collections.defaultdict(list)
    for r in rows:
        by_place[(r.cat, r.sub)].append(r)
    lines.append("## Topics")
    lines.append("")
    lines.append("| Topic | Nodes | What is there |")
    lines.append("|---|---:|---|")
    section = None
    blurb = {c: (s, b) for c, s, b in d.cats}
    for c, s, b in d.cats:
        n = sum(len(v) for (cc, _), v in by_place.items() if cc == c)
        if not n:
            continue
        lines.append(f"| [{md_escape(c)}](#{anchor(c)}) | {n} | {md_escape(b)} |")
    lines.append("")
    for c, sec, b in d.cats:
        cats_rows = [(k, v) for k, v in by_place.items() if k[0] == c]
        if not cats_rows:
            continue
        if sec != section:
            section = sec
            lines.append(f"## {sec}" if sec else "## Topics")
            lines.append("")
        total = sum(len(v) for _, v in cats_rows)
        lines.append(f"### {c}")
        lines.append("")
        lines.append(f"{b} ({total} nodes)")
        lines.append("")
        for (cc, s), rs in sorted(cats_rows, key=lambda kv: sub_index(d, c, kv[0][1])):
            if s:
                sb = next((x[2] for x in d.subs if x[0] == c and x[1] == s), "")
                lines.append(f"#### {c}: {s}")
                lines.append("")
                if sb:
                    lines.append(sb)
                    lines.append("")
            for r in sorted(rs, key=lambda r: ({"Basic": 0, "Intermediate": 1, "Advanced": 2}.get(r.level, 1), r.title.lower())):
                mark = " (Basic)" if r.level == "Basic" else " (Advanced)" if r.level == "Advanced" else ""
                lines.append(f"- {md_escape(r.title or r.id)}{mark} - `{md_escape(r.id)}`")
            lines.append("")


def anchor(text):
    return re.sub(r"[^a-z0-9 -]", "", text.lower()).replace(" ", "-")


def tree(d, out):
    rows = browse_rows(d)
    by_place = collections.Counter(r.place(False) for r in rows)
    section = None
    for c, sec, _ in d.cats:
        n = sum(v for (cc, _), v in by_place.items() if cc == c)
        if sec != section:
            section = sec
            out.append(f"[{sec}]")
        out.append(f"  {c} ({n}, direct {by_place.get((c, ''), 0)})")
        for cc, s, _ in d.subs:
            if cc == c and by_place.get((c, s), 0):
                out.append(f"      {s} ({by_place[(c, s)]})")


# --------------------------------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump", help="the file written by test_vs_categories --dump")
    ap.add_argument("--legacy", action="store_true", help="use the old places (before the split)")
    ap.add_argument("--moved", action="store_true", help="list every node that moved")
    ap.add_argument("--tree", action="store_true", help="print the topic tree with counts")
    ap.add_argument("--markdown-audit", action="store_true", help="print the generated block of docs/visual_script_category_audit.md")
    ap.add_argument("--markdown-topics", action="store_true", help="print docs/visual_scripting_topics.md")
    ap.add_argument("--update", metavar="PAGE", help="with --markdown-audit: rewrite the generated block of this page in place")
    a = ap.parse_args()
    d = Dump(a.dump)
    out = []
    if a.markdown_audit:
        markdown_audit(d, out)
        if a.update:
            update_block(a.update, "\n".join(out))
            print(f"updated {a.update}")
            return
    elif a.markdown_topics:
        markdown_topics(d, out)
    elif a.tree:
        tree(d, out)
    elif a.moved:
        for r in sorted(moved_rows(d), key=lambda r: (r.cat, r.sub, r.title)):
            out.append(f"{r.kind:9s} {r.id:34s} {fmt_place(r.was_cat, r.was_sub):32s} -> {fmt_place(r.cat, r.sub)}")
    else:
        tables(d, a.legacy, out)
        out.append("")
        suspects(d, a.legacy, out)
        out.append("")
        blurb_gaps(d, a.legacy, out)
        out.append("")
        duplicates(d, a.legacy, out)
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
