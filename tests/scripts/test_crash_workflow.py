#!/usr/bin/env python3
"""The crash-reports workflow (.github/workflows/crash-issues.yml) keeps the safety properties the security review asked for.

  python3 -I tests/scripts/test_crash_workflow.py

It reads the file as text (standard library only, no YAML parser) and checks the few lines that matter:
  * the scheduled run is OPT-IN: it only happens when the repository variable CRASH_ISSUES_ENABLED is "true", so merging the workflow does not make it
    fail red every 30 minutes before the owner has run docs/sql/crash_reports.sql; a person can always start it by hand;
  * a manual run is a dry run unless the person unticks the box;
  * the only triggers are the schedule and the manual start (no pull request, push, issue or comment can start it);
  * the permissions are exactly contents: read and issues: write;
  * no secret is ever written into a command line or an expression that a run script would expand: secrets are only in `env:` blocks;
  * the only action is actions/checkout, and the script runs with python -I."""
import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
PATH = os.path.join(ROOT, ".github", "workflows", "crash-issues.yml")
TEXT = open(PATH, encoding="utf-8").read()
LINES = TEXT.split("\n")


def code_lines():
    return [l for l in LINES if not l.strip().startswith("#")]


class Workflow(unittest.TestCase):
    def test_the_scheduled_run_is_opt_in_by_a_repository_variable(self):
        ifs = [l.strip() for l in code_lines() if re.match(r"^\s{4}if:", l)]   # the job's own condition (4 spaces: directly under the job id)
        self.assertEqual(len(ifs), 1, "the job needs one `if:` condition")
        self.assertIn("vars.CRASH_ISSUES_ENABLED == 'true'", ifs[0])
        self.assertIn("github.event_name == 'workflow_dispatch'", ifs[0])      # a person can always start it
        self.assertNotIn("schedule", ifs[0].replace("github.event_name == 'workflow_dispatch'", ""))   # (the variable, not the event name, decides)

    def test_a_manual_run_is_a_dry_run_unless_unticked(self):
        text = "\n".join(code_lines())
        m = re.search(r"dry_run:\s*\n(?:\s+.*\n)*?\s+default:\s*(\w+)", text)
        self.assertTrue(m, "the dry_run input has no default")
        self.assertEqual(m.group(1), "true")
        self.assertRegex(text, r"type:\s*boolean")

    def test_only_the_schedule_and_a_manual_start_can_run_it(self):
        text = "\n".join(code_lines())
        on = re.search(r"^on:\s*\n((?:[ \t]+.*\n|\n)+)", text + "\n", re.M).group(1)
        triggers = re.findall(r"^  ([a-z_]+):", on, re.M)
        self.assertEqual(sorted(triggers), ["schedule", "workflow_dispatch"])
        for bad in ("pull_request", "pull_request_target", "push", "issues", "issue_comment", "workflow_run", "repository_dispatch", "fork"):
            self.assertNotIn(bad, triggers)

    def test_permissions_are_minimal(self):
        text = "\n".join(code_lines())
        block = re.search(r"^permissions:\s*\n((?:[ \t]+\S.*\n)+)", text, re.M).group(1)
        self.assertEqual(sorted(l.strip() for l in block.strip().split("\n")), ["contents: read", "issues: write"])
        self.assertNotIn("write-all", text)

    def test_secrets_are_only_in_env_blocks(self):
        for n, line in enumerate(code_lines()):
            if "secrets." in line:
                self.assertRegex(line, r"^\s+[A-Z_]+:\s*\$\{\{\s*secrets\.[A-Z_]+( != '')?\s*\}\}\s*$", "line %d: a secret outside an env value: %s" % (n, line))
        # no value from the event (inputs, titles, bodies) is expanded inside a run script
        in_run = False
        for line in code_lines():
            if re.match(r"^\s+run:\s*\|?\s*$", line):
                in_run = True
                continue
            if in_run and re.match(r"^\s{0,8}\S", line) and not line.startswith(" " * 11):
                in_run = False
            if in_run:
                self.assertNotIn("${{", line, "an expression inside a run script: " + line)

    def test_only_checkout_and_python_dash_i(self):
        uses = re.findall(r"uses:\s*(\S+)", "\n".join(code_lines()))
        self.assertEqual(uses, ["actions/checkout@v6"])
        runs = re.findall(r"python3?\s+(\S+)", "\n".join(code_lines()))
        self.assertTrue(runs and all(r == "-I" for r in runs), runs)

    def test_the_owner_is_told_the_two_steps(self):
        head = "\n".join(l for l in LINES if l.strip().startswith("#"))
        self.assertIn("docs/sql/crash_reports.sql", head)
        self.assertIn("CRASH_ISSUES_ENABLED", head)


if __name__ == "__main__":
    unittest.main(verbosity=1)
