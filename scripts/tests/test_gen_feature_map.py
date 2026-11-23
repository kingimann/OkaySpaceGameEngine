#!/usr/bin/env python3
"""Tests for the systems list (docs/SYSTEMS.md) of scripts/gen_feature_map.py.
Run: python3 scripts/tests/test_gen_feature_map.py"""
import os, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import gen_feature_map as G


def write(path, text):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)


def sub(sid, title, system, code='`src/a.cpp`', status='Works - read'):
    return (f"### {sid} {title}\n{system}\n- **What:** w\n- **Code:** {code}\n"
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
                      '  - Old Save - Older slots - being merged into Save Games (PR #7) Code: `src/b.cpp`')
                  + sub('1.2', 'Checkpoints', '- **System:** Save Games', status='Partial - gaps'))
        out, problems, counts = G.build(self.d.name)
        md = out['docs/SYSTEMS.md']
        self.assertEqual(problems, [])
        self.assertEqual(counts[3], 2)
        self.assertIn('| **Save Games** | Keeps progress. | 1 Works, 1 Partial | `src/a.cpp` |', md)
        self.assertNotIn('missing.cpp', md)  # paths that do not exist are never written
        self.assertIn('| **Old Save** | Older slots | Works (duplicate', md)
        self.assertIn('| **Old Save** | Save Games | Being merged (PR #7) |', md)
        self.assertIn('[1.2](feature_map/01-core.md#12-checkpoints)', md)
        self.assertIn('- Save Games (1.1, 1.2)', out['docs/feature_map/_index_az.md'])

    def test_warnings(self):
        self.page(sub('1.1', 'A', '- **System:** One - first') + sub('1.2', 'B', '- **System:** One - again')
                  + sub('1.3', 'C', '') + sub('1.4', 'D', '- **System:** Lonely')
                  + sub('1.5', 'E', '- **System:** Two - x - duplicate of Nobody (why)'))
        _, problems, _ = G.build(self.d.name)
        text = '\n'.join(problems)
        self.assertIn("system 'One' is described twice", text)
        self.assertIn("1.3 has no 'System' line", text)
        self.assertIn("system 'Lonely' has no ' - what it does' line", text)
        self.assertIn("system 'Two' says 'duplicate of' but names no other system", text)


if __name__ == '__main__':
    unittest.main()
