#!/usr/bin/env python3
"""Tests for scripts/gen_try_it.py. Run: python3 scripts/tests/test_try_it.py"""
import os, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.dirname(HERE)
sys.path.insert(0, SCRIPTS)
import gen_try_it as G


def entry(eid, row=True, body=True, group=None):
    s = f"---\nid: {eid}\n" + (f"group: {group}\n" if group else "")
    if row:
        s += f"row: | {eid} | what {eid} | where | status |\n"
    return s + "---\n" + (f"## {eid}. Title {eid}\n\ntext {eid}\n" if body else "")


class Tmp(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.TemporaryDirectory()
        self.old = (G.ENTRY_DIR, G.OUT)
        G.ENTRY_DIR = os.path.join(self.d.name, "try_it")
        G.OUT = os.path.join(self.d.name, "try_it.md")
        os.mkdir(G.ENTRY_DIR)
        self.put("_header.md", "# Title\n\nIntro.\n")

    def tearDown(self):
        G.ENTRY_DIR, G.OUT = self.old
        self.d.cleanup()

    def put(self, name, text):
        with open(os.path.join(G.ENTRY_DIR, name), "w", encoding="utf-8") as f:
            f.write(text)


class Generator(Tmp):
    def test_sorted_naturally_and_deterministic(self):
        for eid in ("16", "7zh", "7b", "7", "2"):
            self.put(f"{eid}-x.md", entry(eid))
        a = G.assemble()
        rows = [l.split("|")[1].strip() for l in a.split("\n") if l.startswith("| ") and "what " in l]
        self.assertEqual(rows, ["2", "7", "7b", "7zh", "16"])
        self.assertEqual(a, G.assemble())
        self.assertLess(a.index("## 7. Title"), a.index("## 16. Title"))

    def test_groups_order_and_row_only_or_body_only(self):
        self.put("a.md", entry("1", group="appendix"))
        self.put("b.md", entry("9", group="switches"))
        self.put("c.md", entry("20"))
        self.put("d.md", entry("5", body=False))
        self.put("e.md", entry("6", row=False))
        a = G.assemble()
        self.assertLess(a.index("## 20."), a.index("## 9."))
        self.assertLess(a.index("## 9."), a.index("## 1."))
        self.assertIn("| 5 | what 5 |", a)
        self.assertNotIn("| 6 | what 6 |", a)
        self.assertIn("## 6. Title 6", a)

    def test_order_independent_of_file_names(self):
        self.put("zzz.md", entry("1"))
        self.put("aaa.md", entry("2"))
        self.assertLess(G.assemble().index("## 1."), G.assemble().index("## 2."))

    def test_errors(self):
        bad = {
            "dup2.md": entry("1"),
            "nofm.md": "## no front matter\n",
            "badrow.md": "---\nid: 3\nrow: | 4 | a | b | c |\n---\n",
            "cells.md": "---\nid: 3\nrow: | 3 | a | b |\n---\n",
            "empty.md": "---\nid: 3\n---\n",
            "nohead.md": "---\nid: 3\n---\njust text\n",
            "grp.md": "---\nid: 3\ngroup: nope\nrow: | 3 | a | b | c |\n---\n",
            "key.md": "---\nid: 3\nfoo: 1\nrow: | 3 | a | b | c |\n---\n",
        }
        for name, text in bad.items():
            with self.subTest(name):
                self.put("ok.md", entry("1"))
                self.put(name, text)
                with self.assertRaises(ValueError):
                    G.assemble()
                os.remove(os.path.join(G.ENTRY_DIR, name))

    def test_check_modes_and_conflict_resolution(self):
        self.put("a.md", entry("1"))
        self.assertEqual(G.main([]), 0)
        self.assertEqual(G.main(["--check"]), 0)
        with open(G.OUT, "a") as f:
            f.write("<<<<<<< HEAD\nx\n=======\ny\n>>>>>>> main\n")
        self.assertEqual(G.main(["--check"]), 1)      # stale / conflicted page is detected
        self.assertEqual(G.main(["--check-entries"]), 0)  # but entries are fine
        self.assertEqual(G.main([]), 0)               # regenerating resolves it
        self.assertEqual(G.main(["--check"]), 0)
        self.assertNotIn("<<<<<<<", open(G.OUT).read())

    def test_new_scaffold_is_valid(self):
        self.assertEqual(G.main(["--new", "7zz", "A New Thing"]), 0)
        self.assertEqual(G.main(["--check-entries"]), 0)


class RealRepo(unittest.TestCase):
    def test_real_entries_valid(self):
        # Entries only: feature PRs do not regenerate docs/try_it.md, so freshness is not a per-PR test.
        self.assertEqual(G.main(["--check-entries"]), 0)


if __name__ == "__main__":
    unittest.main()
