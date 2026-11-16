#!/usr/bin/env python3
"""The account server address and key are not stored in the repository; they come in at configure time.

  python3 -I tests/scripts/test_account_config.py

  * no tracked source file (CMake, launcher, editor, shared, scripts, workflows, the reset page) names a real project address or holds a key;
  * the CMake block that reads OKAY_DEFAULT_ACCOUNT_URL / _KEY is run on its own in a throw-away project (no compiler, no downloads):
      - nothing given        -> configures fine, no definitions, says "NO";
      - -D values            -> both become compile definitions;
      - environment values   -> the same;
      - the key never appears in the configure output.
Skipped (with a note) when cmake is not installed."""
import os, re, shutil, subprocess, sys, tempfile, unittest

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
START, END = "# ---- Optional baked-in account server", "# Hosted password-reset page"
DUMMY_URL, DUMMY_KEY = "https://dummyproj123.supabase.co", "sb_publishable_DUMMYKEYDUMMYKEY"   # lint-allow: secret (made up)


def tracked_text_files():
    out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True).stdout.decode().split("\0")
    for rel in out:
        if rel.startswith(("CMakeLists.txt", "cmake/", "launcher/", "editor/", "shared/", "scripts/", ".github/", "supabase/", "player/", "engine/src/Platform/")) \
                and rel.endswith((".txt", ".cmake", ".cpp", ".hpp", ".h", ".sh", ".py", ".yml", ".html", ".inc")):
            yield rel


class NoStoredServer(unittest.TestCase):
    def test_no_real_project_address_or_key_in_tracked_source(self):
        addr = re.compile(r"https://(?!abc|abcdefghij|dummy|demo|xyz|YOUR|<|\$)[a-z0-9]{16,}\.supabase\.co")
        key = re.compile(r"sb_publishable_[A-Za-z0-9_]{20,}|eyJ[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{10,}\.")
        bad = []
        for rel in tracked_text_files():
            if rel.startswith("scripts/tests/") or rel.startswith("tests/"):
                continue
            try:
                with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as fh:
                    text = fh.read()
            except OSError:
                continue
            if addr.search(text) or key.search(text):
                bad.append(rel)
        self.assertEqual(bad, [], "a project address or key is stored in: %s" % bad)


@unittest.skipUnless(shutil.which("cmake"), "cmake is not installed")
class ConfigureBlock(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8") as fh:
            text = fh.read()
        block = text[text.index(START):text.index(END)]
        cls.tmp = tempfile.mkdtemp()
        with open(os.path.join(cls.tmp, "CMakeLists.txt"), "w") as f:
            f.write("cmake_minimum_required(VERSION 3.16)\nproject(Probe LANGUAGES NONE)\n" + block +
                    'get_directory_property(_d COMPILE_DEFINITIONS)\nmessage(STATUS "DEFS=[${_d}]")\n')

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def configure(self, args=(), env=None):
        b = tempfile.mkdtemp(dir=self.tmp)
        e = {k: v for k, v in os.environ.items() if not k.startswith("OKAY_DEFAULT_ACCOUNT")}
        e.update(env or {})
        p = subprocess.run(["cmake", "-S", self.tmp, "-B", b] + list(args), capture_output=True, text=True, env=e)
        self.assertEqual(p.returncode, 0, p.stderr)
        return p.stdout + p.stderr

    def test_empty_still_configures(self):
        out = self.configure()
        self.assertIn("Account server baked in: NO", out)
        self.assertIn("DEFS=[]", out)

    def test_cmake_options(self):
        out = self.configure(["-DOKAY_DEFAULT_ACCOUNT_URL=" + DUMMY_URL, "-DOKAY_DEFAULT_ACCOUNT_KEY=" + DUMMY_KEY])
        self.assertIn("Account server baked in: yes", out)
        self.assertIn('OKAY_DEFAULT_ACCOUNT_URL="%s"' % DUMMY_URL, out)
        self.assertIn('OKAY_DEFAULT_ACCOUNT_KEY="%s"' % DUMMY_KEY, out)   # (the throw-away project prints the definitions; the real one only says yes/no)

    def test_environment(self):
        out = self.configure(env={"OKAY_DEFAULT_ACCOUNT_URL": DUMMY_URL, "OKAY_DEFAULT_ACCOUNT_KEY": DUMMY_KEY})
        self.assertIn("Account server baked in: yes", out)
        self.assertIn(DUMMY_URL, out)

    def test_status_lines_never_print_the_key(self):
        out = self.configure(env={"OKAY_DEFAULT_ACCOUNT_URL": DUMMY_URL, "OKAY_DEFAULT_ACCOUNT_KEY": DUMMY_KEY})
        status = [l for l in out.splitlines() if "Account" in l]
        self.assertTrue(status)
        self.assertFalse([l for l in status if DUMMY_KEY in l], status)


if __name__ == "__main__":
    unittest.main()
