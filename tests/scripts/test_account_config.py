#!/usr/bin/env python3
"""The account server address and the publishable key live in ONE file, config/public-account.json.

  python3 -I tests/scripts/test_account_config.py

  * no other tracked source file (CMake, launcher, editor, shared, scripts, workflows, the reset page) holds a project address or a key;
  * the file is valid JSON with a url and a key;
  * the CMake block that reads it is run on its own in a throw-away project (no compiler, no downloads):
      - file present                -> baked in "yes (from config/public-account.json)";
      - file absent, nothing given  -> still configures, no definitions, says "NO";
      - -D values / environment     -> win over the file;
      - the key never appears in a status line;
  * scripts/public-account.sh reads the same file.
Skipped (with a note) when cmake is not installed."""
import json, os, re, shutil, subprocess, sys, tempfile, unittest

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
START, END = "# ---- Baked-in account server", "# Hosted password-reset page"
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
            if rel.startswith(("scripts/tests/", "tests/", "config/")):
                continue
            try:
                with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as fh:
                    text = fh.read()
            except OSError:
                continue
            if addr.search(text) or key.search(text):
                bad.append(rel)
        self.assertEqual(bad, [], "a project address or key is stored in: %s" % bad)


class TheFile(unittest.TestCase):
    def test_valid(self):
        with open(os.path.join(ROOT, "config", "public-account.json"), encoding="utf-8") as fh:
            d = json.load(fh)
        self.assertTrue(re.match(r"^https://[a-z0-9]+\.supabase\.co$", d.get("url", "")), "url is missing or not an https://<project>.supabase.co address")
        self.assertTrue(d.get("key"), "key is empty")
        self.assertTrue(os.path.exists(os.path.join(ROOT, "config", "README.md")))

    def test_shell_helper_reads_it(self):
        with open(os.path.join(ROOT, "config", "public-account.json"), encoding="utf-8") as fh:
            d = json.load(fh)
        run = lambda what, env=None: subprocess.run(["bash", "-c", '. "%s/scripts/public-account.sh"; okay_public_account %s' % (ROOT, what)],
                                                    capture_output=True, text=True, env=dict(os.environ, **(env or {}))).stdout.strip()
        self.assertEqual(run("url"), d["url"])
        self.assertEqual(run("key"), d["key"])
        self.assertEqual(run("url", {"OKAY_PUBLIC_ACCOUNT_FILE": "/no/such/file"}), "")


@unittest.skipUnless(shutil.which("cmake"), "cmake is not installed")
class ConfigureBlock(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8") as fh:
            text = fh.read()
        cls.block = text[text.index(START):text.index(END)]
        with open(os.path.join(ROOT, "config", "public-account.json"), encoding="utf-8") as fh:
            cls.real = json.load(fh)
        cls.tmp = tempfile.mkdtemp()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def project(self, cfg):
        """A throw-away source tree with the real block, and config/public-account.json = cfg (None = no file)."""
        d = tempfile.mkdtemp(dir=self.tmp)
        with open(os.path.join(d, "CMakeLists.txt"), "w") as f:
            f.write("cmake_minimum_required(VERSION 3.16)\nproject(Probe LANGUAGES NONE)\n" + self.block +
                    'get_directory_property(_d COMPILE_DEFINITIONS)\nmessage(STATUS "DEFS=[${_d}]")\n')
        if cfg is not None:
            os.makedirs(os.path.join(d, "config"))
            with open(os.path.join(d, "config", "public-account.json"), "w") as f:
                json.dump(cfg, f)
        return d

    def configure(self, cfg, args=(), env=None):
        src = self.project(cfg)
        e = {k: v for k, v in os.environ.items() if not k.startswith("OKAY_DEFAULT_ACCOUNT")}
        e.update(env or {})
        p = subprocess.run(["cmake", "-S", src, "-B", os.path.join(src, "b")] + list(args), capture_output=True, text=True, env=e)
        self.assertEqual(p.returncode, 0, p.stderr)
        return p.stdout + p.stderr

    def test_file_present_is_baked_in(self):
        out = self.configure({"url": DUMMY_URL, "key": DUMMY_KEY})
        self.assertIn("Account server baked in: yes (from config/public-account.json)", out)
        self.assertIn('OKAY_DEFAULT_ACCOUNT_URL="%s"' % DUMMY_URL, out)
        self.assertIn('OKAY_DEFAULT_ACCOUNT_KEY="%s"' % DUMMY_KEY, out)   # (the probe prints the definitions; the real project only says yes/no)

    def test_the_real_file_is_found(self):
        out = self.configure(self.real)
        self.assertIn("Account server baked in: yes", out)

    def test_file_absent_still_configures_empty(self):
        out = self.configure(None)
        self.assertIn("Account server baked in: NO", out)
        self.assertIn("DEFS=[]", out)

    def test_empty_file_values_configure_empty(self):
        self.assertIn("Account server baked in: NO", self.configure({"url": "", "key": ""}))

    def test_d_option_wins(self):
        out = self.configure({"url": DUMMY_URL, "key": DUMMY_KEY}, ["-DOKAY_DEFAULT_ACCOUNT_URL=https://override99.supabase.co", "-DOKAY_DEFAULT_ACCOUNT_KEY=OVERRIDEKEY"])
        self.assertIn("(from -D option)", out)
        self.assertIn('OKAY_DEFAULT_ACCOUNT_URL="https://override99.supabase.co"', out)
        self.assertNotIn(DUMMY_URL, out)

    def test_environment_wins_over_file(self):
        out = self.configure({"url": DUMMY_URL, "key": DUMMY_KEY}, env={"OKAY_DEFAULT_ACCOUNT_URL": "https://envproj77.supabase.co", "OKAY_DEFAULT_ACCOUNT_KEY": "ENVKEY"})
        self.assertIn("(from environment)", out)
        self.assertIn("https://envproj77.supabase.co", out)
        self.assertNotIn(DUMMY_URL, out)

    def test_empty_environment_means_use_the_file(self):
        out = self.configure({"url": DUMMY_URL, "key": DUMMY_KEY}, env={"OKAY_DEFAULT_ACCOUNT_URL": "", "OKAY_DEFAULT_ACCOUNT_KEY": ""})
        self.assertIn("(from config/public-account.json)", out)

    def test_status_lines_never_print_the_key(self):
        out = self.configure({"url": DUMMY_URL, "key": DUMMY_KEY})
        status = [l for l in out.splitlines() if "Account" in l]
        self.assertTrue(status)
        self.assertFalse([l for l in status if DUMMY_KEY in l], status)


if __name__ == "__main__":
    unittest.main()
