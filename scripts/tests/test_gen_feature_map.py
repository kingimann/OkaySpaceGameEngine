#!/usr/bin/env python3
"""Tests for the systems list (docs/SYSTEMS.md) and the --validate mode of scripts/gen_feature_map.py.
Run: python3 scripts/tests/test_gen_feature_map.py"""
import io, os, sys, tempfile, unittest
from contextlib import redirect_stderr, redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import gen_feature_map as G

# a system's describing line must give its registry fields on the next line (docs/feature_map/README.md, #1169)
SAVES = '- **System:** Save Games - Keeps progress.\n  Home: `src/a.cpp`. Entry: `src/a.cpp`.'


def write(path, text):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)


def sub(sid, title, system, code='`src/a.cpp`', status='Works - read', docs='-'):
    return (f"### {sid} {title}\n{system}\n- **What:** w\n- **Code:** {code}\n- **Docs:** {docs}\n"
            f"- **Status:** {status}\n- **Words:** x\n\n")


class Systems(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.TemporaryDirectory()
        r = self.d.name
        os.makedirs(os.path.join(r, 'docs', 'feature_map'))
        os.makedirs(os.path.join(r, 'src'))
        for f in ('a.cpp', 'b.cpp', 'c.cpp'):
            write(os.path.join(r, 'src', f), '')
        for f, t in (('_header.md', '# Map\n{{STATS}}\n'), ('_footer.md', 'end\n')):
            write(os.path.join(r, 'docs', 'feature_map', f), t)

    def tearDown(self):
        self.d.cleanup()

    def page(self, text):
        write(os.path.join(self.d.name, 'docs', 'feature_map', '01-core.md'), '# 1. Core\n\n## 1. Core\n\n' + text)

    def test_table_status_code_and_duplicates(self):
        self.page(sub('1.1', 'Saves', '- **System:**\n'
                      '  - Save Games - Keeps progress. Code: `src/a.cpp`, `src/missing.cpp`\n'
                      '    Home: `src/`. Entry: `src/a.cpp`. Also called: Old Saves, `SaveFramework`.\n'
                      '  - Old Save - Older slots - being merged into Save Games (PR #7) Code: `src/b.cpp`\n'
                      '    Home: `src/b.cpp`. Entry: `src/b.cpp`. Fix: delete it after the merge.',
                      docs='`docs/saves.md`')
                  + sub('1.2', 'Checkpoints', '- **System:** Save Games', status='Partial - gaps'))
        write(os.path.join(self.d.name, 'docs', 'saves.md'), '# Saves\n')
        out, problems, counts = G.build(self.d.name)
        md = out['docs/SYSTEMS.md']
        self.assertEqual(problems, [])
        self.assertEqual(counts[3], 2)
        self.assertIn('| **Save Games** | Keeps progress. | `src/` | `src/a.cpp` | [saves.md](saves.md) | - | '
                      'Old Saves, SaveFramework | 1 Works, 1 Partial |', md)
        self.assertIn('| **Old Save** | Older slots | `src/b.cpp` | `src/b.cpp` | [saves.md](saves.md) | - | - | Works (see', md)
        self.assertIn('| **Old Save** | Same job as **Save Games** | Being merged (PR #7). delete it after the merge |', md)
        self.assertIn('[1.2](feature_map/01-core.md#12-checkpoints)', md)
        az = out['docs/feature_map/_index_az.md']
        self.assertIn('- Save Games (1.1, 1.2)', az)
        self.assertIn('- Old Saves (1.1, 1.2)', az)  # an old name leads to the system
        reg = {e['name']: e for e in G.registry(self.d.name)}
        self.assertEqual(reg['Save Games']['aliases'], ['Old Saves', 'SaveFramework'])
        self.assertEqual(reg['Save Games']['entry'], 'src/a.cpp')

    def test_two_homes_go_into_still_duplicated(self):
        os.makedirs(os.path.join(self.d.name, 'editor'))
        write(os.path.join(self.d.name, 'editor', 'W.cpp'), '')
        self.page(sub('1.1', 'A', '- **System:** Split - two homes\n  Home: `src/a.cpp`, `docs/feature_map/`. Entry: `src/a.cpp`.')
                  + sub('1.2', 'B', '- **System:** Tool - one home and its editor window\n  Home: `src/b.cpp`, `editor/W.cpp`. Entry: `src/b.cpp`.'))
        out, problems, _ = G.build(self.d.name)
        md = out['docs/SYSTEMS.md']
        self.assertEqual(problems, [])
        self.assertIn('| **Split** | Code in 2 homes: `src`, `docs` | Pick one home folder', md)
        self.assertNotIn('| **Tool** | Code in', md)  # an editor window is not a second home

    def test_warnings(self):
        self.page(sub('1.1', 'A', '- **System:** One - first') + sub('1.2', 'B', '- **System:** One - again')
                  + sub('1.3', 'C', '') + sub('1.4', 'D', '- **System:** Lonely')
                  + sub('1.5', 'E', '- **System:** Two - x - duplicate of Nobody (why)')
                  + sub('1.6', 'F', '- **System:** Three - y\n  Home: `src/nothere/`. Entry: `src/c.cpp`. Also called: First, One')
                  + sub('1.7', 'G', '- **System:** Physics V2 - z'))
        _, problems, _ = G.build(self.d.name)
        text = '\n'.join(problems)
        self.assertIn("system 'Three' names `src/nothere/`, which does not exist", text)
        self.assertIn("'One' is a name or alias of both 'Three' and 'One'", text)
        self.assertIn("system 'One' has no 'Home:'", text)
        self.assertIn("system 'One' has no 'Entry:'", text)
        self.assertIn("system 'One' is described twice", text)
        self.assertIn("system name 'Physics V2' has 'V2' in it", text)
        self.assertIn("1.3 has no 'System' line", text)
        self.assertIn("system 'Lonely' has no ' - what it does' line", text)
        self.assertIn("system 'Two' says 'duplicate of' but names no other system", text)


class Validate(unittest.TestCase):
    """--validate checks the hand-written pages only: no file is written or compared (PRs do not commit the generated docs)."""
    setUp, tearDown, page = Systems.setUp, Systems.tearDown, Systems.page

    def run_main(self, *args):
        old = sys.argv
        sys.argv = ['gen_feature_map.py', '--root', self.d.name, *args]
        out, err = io.StringIO(), io.StringIO()
        try:
            with redirect_stdout(out), redirect_stderr(err):
                rc = G.main()
        finally:
            sys.argv = old
        return rc, out.getvalue(), err.getvalue()

    def generated(self):
        return [p for p in ('docs/FEATURE_MAP.md', 'docs/SYSTEMS.md', 'docs/feature_map/_index_az.md',
                            'docs/feature_map/_index_symptoms.md') if os.path.exists(os.path.join(self.d.name, p))]

    def test_valid_pages_pass_and_nothing_is_written(self):
        self.page(sub('1.1', 'Saves', SAVES))
        rc, out, err = self.run_main('--validate')
        self.assertEqual(rc, 0, err)
        self.assertIn('pages valid (1 areas, 1 sub-areas', out)
        self.assertEqual(self.generated(), [])

    def test_stale_generated_files_do_not_matter(self):
        self.page(sub('1.1', 'Saves', SAVES))
        write(os.path.join(self.d.name, 'docs', 'FEATURE_MAP.md'), 'old copy from an older main\n')
        self.assertEqual(self.run_main('--check')[0], 1)      # the old check would fail ...
        self.assertEqual(self.run_main('--validate')[0], 0)   # ... the PR check does not
        with open(os.path.join(self.d.name, 'docs', 'FEATURE_MAP.md'), encoding='utf-8') as f:
            self.assertEqual(f.read(), 'old copy from an older main\n')

    def test_problems_fail(self):
        self.page("### 1.1 Saves\n" + SAVES + "\n- **What:** w\n- **Code:** `src/a.cpp`\n"
                  "- **Status:** Works\n\n" + sub('1.2', 'No system', ''))
        rc, out, err = self.run_main('--validate')
        self.assertEqual(rc, 1)
        self.assertIn("error: 01-core.md:5: 1.1 is missing 'Words'", err)
        self.assertIn("1.2 has no 'System' line", err)
        self.assertIn('2 problem(s)', err)
        self.assertEqual(self.generated(), [])


if __name__ == '__main__':
    unittest.main()
