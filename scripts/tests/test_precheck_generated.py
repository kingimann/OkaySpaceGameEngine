#!/usr/bin/env python3
"""Tests for "generated docs are rebuilt on main only": scripts/precheck.sh (--list-generated, --regen-generated,
--restore-generated, the source check and the warning when a branch commits generated docs) and the commit-back step of
.github/workflows/regen-docs.yml, run for real against a throw-away "origin" (a bare git repository; no GitHub).
Run: python3 scripts/tests/test_precheck_generated.py"""
import json, os, re, shutil, subprocess, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
WORKFLOW = os.path.join(ROOT, '.github', 'workflows', 'regen-docs.yml')
GEN_SCRIPTS = ('precheck.sh', 'gen_feature_map.py', 'gen_code_index.py', 'gen_try_it.py')

PAGE = ("# 1. Core\n\n## 1. Core\n\n### 1.1 Saves\n- **System:** Save Games - Keeps progress.\n"
        "  Home: `engine/include/okay/Save.hpp`. Entry: `engine/include/okay/Save.hpp`.\n- **What:** w\n"
        "- **Code:** `engine/include/okay/Save.hpp`\n- **Status:** Works - read\n- **Words:** save\n")
BAD_PAGE = PAGE.replace("- **Words:** save\n", "")


def git(cwd, *args, check=True):
    r = subprocess.run(['git', *args], cwd=cwd, capture_output=True, text=True)
    if check and r.returncode:
        raise AssertionError(f"git {' '.join(args)} failed: {r.stderr}")
    return r.stdout


def write(root, rel, text):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


def read(root, rel):
    with open(os.path.join(root, rel), encoding='utf-8') as f:
        return f.read()


def workflow_script():
    """The shell text of the 'Regenerate and commit back to main' step."""
    text = read(ROOT, WORKFLOW)
    m = re.search(r"- name: Regenerate and commit back to main\n\s+run: \|\n((?:          .*\n|\s*\n)+)", text)
    assert m, 'step not found in regen-docs.yml'
    return '\n'.join(line[10:] for line in m.group(1).split('\n'))


class Repo(unittest.TestCase):
    """A tiny repository with the real scripts and one source of each kind, pushed to a bare origin."""

    def setUp(self):
        self.d = tempfile.TemporaryDirectory()
        self.origin = os.path.join(self.d.name, 'origin.git')
        self.work = os.path.join(self.d.name, 'work')
        git(self.d.name, 'init', '-q', '--bare', '-b', 'main', self.origin)
        git(self.d.name, 'clone', '-q', self.origin, self.work)
        self.ident(self.work)
        for s in GEN_SCRIPTS:
            os.makedirs(os.path.join(self.work, 'scripts'), exist_ok=True)
            shutil.copy(os.path.join(ROOT, 'scripts', s), os.path.join(self.work, 'scripts', s))
        write(self.work, 'docs/feature_map/_header.md', '# Map\n{{STATS}}\n')
        write(self.work, 'docs/feature_map/_footer.md', 'end\n')
        write(self.work, 'docs/feature_map/01-core.md', PAGE)
        write(self.work, 'engine/include/okay/Save.hpp', '#pragma once\nnamespace okay {\n/// Saves the game.\nstruct SaveGame {};\n}\n')
        write(self.work, 'docs/try_it/_header.md', '# Try it\n')
        write(self.work, 'docs/try_it/1-saves.md', '---\nid: 1\nrow: | 1 | Saves | File menu | Works |\n---\n## 1. Saves\n\nSave.\n')
        self.run_sh(self.work, '--regen-generated')
        git(self.work, 'add', '-A')
        git(self.work, 'commit', '-q', '-m', 'start')
        git(self.work, 'push', '-q', 'origin', 'main')

    def tearDown(self):
        self.d.cleanup()

    @staticmethod
    def ident(cwd):
        git(cwd, 'config', 'user.name', 'Test')
        git(cwd, 'config', 'user.email', 'test@example.invalid')

    def run_sh(self, cwd, *args, ok=True):
        r = subprocess.run(['bash', 'scripts/precheck.sh', *args], cwd=cwd, capture_output=True, text=True)
        if ok and r.returncode:
            raise AssertionError(f"precheck {' '.join(args)} exit {r.returncode}:\n{r.stdout}\n{r.stderr}")
        return r


class Precheck(Repo):
    def test_list_generated_matches_lanes_json(self):
        listed = self.run_sh(self.work, '--list-generated').stdout.split()
        for p in ('docs/FEATURE_MAP.md', 'docs/SYSTEMS.md', 'docs/feature_map/_index_az.md',
                  'docs/feature_map/_index_symptoms.md', 'docs/engine/code_index.md', 'docs/engine/code_index', 'docs/try_it.md'):
            self.assertIn(p, listed)
        # every lanes.json "generated" entry that says it is rebuilt on main is in the list, and the other way round
        lanes = json.loads(read(ROOT, 'docs/lanes.json'))
        on_main = {g['file'] for g in lanes['generated'] if 'restore-generated' in g['regen']}
        self.assertEqual(on_main - set(listed), set())
        self.assertEqual({p for p in listed if p.endswith('.md')} - on_main, set())

    def test_regen_wrote_every_generated_doc(self):
        for p in ('docs/FEATURE_MAP.md', 'docs/SYSTEMS.md', 'docs/engine/code_index.md', 'docs/try_it.md'):
            self.assertTrue(os.path.exists(os.path.join(self.work, p)), p)
        self.assertIn('Save Games', read(self.work, 'docs/SYSTEMS.md'))
        self.assertIn('code_index/engine-root.md', read(self.work, 'docs/engine/code_index.md'))
        self.assertIn('SaveGame', read(self.work, 'docs/engine/code_index/engine-root.md'))

    def test_restore_generated(self):
        git(self.work, 'checkout', '-q', '-b', 'claude2/x')
        main_map, main_sys = read(self.work, 'docs/FEATURE_MAP.md'), read(self.work, 'docs/SYSTEMS.md')
        write(self.work, 'docs/feature_map/01-core.md', PAGE + '\nmore\n')               # a source: kept
        write(self.work, 'docs/FEATURE_MAP.md', 'branch copy\n')                         # committed generated change
        write(self.work, 'docs/engine/code_index/extra.md', 'committed new page\n')      # committed new generated page
        git(self.work, 'add', '-A')
        git(self.work, 'commit', '-q', '-m', 'branch work')
        write(self.work, 'docs/SYSTEMS.md', 'uncommitted\n')                             # uncommitted change
        write(self.work, 'docs/engine/code_index/untracked.md', 'x\n')                   # untracked new page
        out = self.run_sh(self.work, '--restore-generated').stdout
        self.assertIn('restored: docs/FEATURE_MAP.md', out)
        self.assertEqual(read(self.work, 'docs/FEATURE_MAP.md'), main_map)
        self.assertEqual(read(self.work, 'docs/SYSTEMS.md'), main_sys)
        self.assertFalse(os.path.exists(os.path.join(self.work, 'docs/engine/code_index/extra.md')))
        self.assertFalse(os.path.exists(os.path.join(self.work, 'docs/engine/code_index/untracked.md')))
        self.assertIn('more', read(self.work, 'docs/feature_map/01-core.md'))
        git(self.work, 'commit', '-q', '-am', 'use main copy')
        self.assertEqual(git(self.work, 'diff', '--name-only', 'origin/main...HEAD').split(), ['docs/feature_map/01-core.md'])
        self.assertIn('already match', self.run_sh(self.work, '--restore-generated').stdout)

    def test_full_run_checks_sources_and_warns_about_generated(self):
        git(self.work, 'checkout', '-q', '-b', 'claude2/y')
        # only the source changed and the generated copies are now stale: no failure for the map, no warning
        write(self.work, 'docs/feature_map/01-core.md', PAGE.replace('- **Words:** save', '- **Words:** save, load'))
        git(self.work, 'commit', '-q', '-am', 'source only')
        out = self.run_sh(self.work, ok=False).stdout
        self.assertIn('ok:   feature map pages valid', out)
        self.assertNotIn('changes generated docs', out)
        # a generated doc committed in the branch: a warning with the restore hint, not a failure
        write(self.work, 'docs/FEATURE_MAP.md', 'branch copy\n')
        git(self.work, 'commit', '-q', '-am', 'generated too')
        out = self.run_sh(self.work, ok=False).stdout
        self.assertIn('warn: this branch changes generated docs: docs/FEATURE_MAP.md', out)
        self.assertIn('--restore-generated', out)
        self.assertNotIn('FAIL: feature map', out)
        # a broken source page fails
        write(self.work, 'docs/feature_map/01-core.md', BAD_PAGE)
        git(self.work, 'commit', '-q', '-am', 'broken page')
        out = self.run_sh(self.work, ok=False).stdout
        self.assertIn('FAIL: feature map pages', out)
        self.assertIn("1.1 is missing 'Words'", out)


class Workflow(Repo):
    def run_workflow(self, clone):
        r = subprocess.run(['bash', '-c', workflow_script()], cwd=clone, capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr

    def fresh_clone(self, name):
        path = os.path.join(self.d.name, name)
        git(self.d.name, 'clone', '-q', self.origin, path)
        self.ident(path)
        return path

    def push_source_change(self, words):
        write(self.work, 'docs/feature_map/01-core.md', PAGE.replace('- **Words:** save', f'- **Words:** {words}'))
        git(self.work, 'commit', '-q', '-am', f'source {words}')
        git(self.work, 'push', '-q', 'origin', 'main')

    def test_properties(self):
        text = read(ROOT, WORKFLOW)
        self.assertRegex(text, r"on:\n  push:\n    branches: \[main\]")
        self.assertIn('contents: write', text)
        self.assertRegex(text, r"concurrency:\n  group: regen-docs\n  cancel-in-progress: false")
        self.assertIn('[skip ci]', text)
        self.assertIn("github.actor != 'github-actions[bot]'", text)
        self.assertNotIn('--force', text)
        self.assertNotIn('secrets.', text)   # the workflow's own token only

    def test_commits_back_once_then_nothing_to_do(self):
        self.push_source_change('save, load')
        ci = self.fresh_clone('ci')
        rc, out = self.run_workflow(ci)
        self.assertEqual(rc, 0, out)
        self.assertIn('pushed', out)
        log = git(self.origin, 'log', '-1', '--format=%s%n%an', 'main')
        self.assertIn('[skip ci]', log)
        self.assertIn('github-actions[bot]', log)
        files = git(self.origin, 'show', '--name-only', '--format=', 'main').split()
        self.assertIn('docs/feature_map/_index_az.md', files)
        self.assertTrue(all(f in ('docs/FEATURE_MAP.md', 'docs/SYSTEMS.md', 'docs/feature_map/_index_az.md',
                                  'docs/feature_map/_index_symptoms.md') for f in files), files)
        self.assertIn('load', git(self.origin, 'show', 'main:docs/feature_map/_index_az.md'))
        head = git(self.origin, 'rev-parse', 'main')
        rc, out = self.run_workflow(ci)
        self.assertEqual(rc, 0, out)
        self.assertIn('up to date', out)
        self.assertEqual(git(self.origin, 'rev-parse', 'main'), head)

    def test_main_moved_while_running_retries_from_new_main(self):
        self.push_source_change('save, load')
        ci = self.fresh_clone('ci')
        # the first push from the CI clone is refused because someone merged a PR a moment before it
        other = self.fresh_clone('other')
        hook = os.path.join(ci, '.git', 'hooks', 'pre-push')
        marker = os.path.join(self.d.name, 'moved')
        with open(hook, 'w') as f:
            f.write(f"#!/bin/sh\n[ -e '{marker}' ] && exit 0\ntouch '{marker}'\n"
                    f"cd '{other}' && echo pr > pr.txt && git add pr.txt && git commit -q -m 'a PR' && git push -q origin main\n")
        os.chmod(hook, 0o755)
        rc, out = self.run_workflow(ci)
        self.assertEqual(rc, 0, out)
        self.assertIn('main moved while regenerating (attempt 1)', out)
        subjects = git(self.origin, 'log', '--format=%s', '-3', 'main').split('\n')
        self.assertIn('[skip ci]', subjects[0])
        self.assertEqual(subjects[1], 'a PR')   # regenerated on top of the new main, the PR's commit is kept

    def test_refuses_changes_outside_the_generated_list(self):
        # a generator that also touches a source must not be committed to main
        text = read(self.work, 'scripts/gen_try_it.py').replace(
            'if __name__ == "__main__":', "open(os.path.join(ROOT, 'README.md'), 'w').close()\n\nif __name__ == \"__main__\":")
        write(self.work, 'scripts/gen_try_it.py', text)
        self.push_source_change('save, load')
        ci = self.fresh_clone('ci')
        head = git(self.origin, 'rev-parse', 'main')
        rc, out = self.run_workflow(ci)
        self.assertEqual(rc, 1, out)
        self.assertIn('changed files outside the generated list', out)
        self.assertEqual(git(self.origin, 'rev-parse', 'main'), head)


if __name__ == '__main__':
    unittest.main()
