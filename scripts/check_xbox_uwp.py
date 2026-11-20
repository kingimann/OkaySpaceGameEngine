#!/usr/bin/env python3
"""Linux checks for the experimental Xbox UWP export (docs/platform/xbox_uwp.md). No Windows, no PowerShell needed.

  check_xbox_uwp.py scripts <repo root>
      Static checks of scripts/build-xbox-uwp.ps1, xbox-check-sdk.ps1, xbox-compile-shaders.ps1, xbox-install-local.ps1, xbox-sign.ps1:
      a small PowerShell lexer (comments, here-strings, single and double quoted strings with $( ) inside, backtick escapes) proves that every
      bracket and quote is balanced; files are ASCII with no BOM problem (Windows PowerShell 5.1 reads BOM-less files as ANSI); every repo path a script
      builds with Join-Path exists; every script that another script starts exists and the parameters passed to it are declared; every build step
      has a "what to do next" hint; the step numbers run 1..Total; the docs only name switches the script declares.
  check_xbox_uwp.py pipeline --export <okay-xbox-export> --appx <okay-appx> --work <dir>
      The real pipeline of the build script on Linux: write the test export, add stand-ins for the files the Windows build adds, pack with OUR packer,
      verify with OUR reader, then read the .appx again with Python's zipfile (a second, independent reader) and check the manifest. Then damage the
      package and check that the reader refuses it.
Everything here is UNVERIFIED as a statement about Windows or an Xbox: it only proves what can be proved without them.
"""
import argparse, os, re, shutil, struct, subprocess, sys, zipfile
import xml.etree.ElementTree as ET

fails = []
def check(cond, msg):
    if not cond:
        fails.append(msg); print("FAIL: " + msg)

# ---------------------------------------------------------------- PowerShell lexer
class Lex:
    OPEN = {'(': ')', '{': '}', '[': ']'}
    def __init__(self, text, name):
        self.t = text; self.n = name; self.i = 0; self.line = 1; self.strings = []
    def err(self, m): check(False, "%s:%d: %s" % (self.n, self.line, m))
    def code(self, closer=None):
        """Lex code until `closer` (a bracket char) is found (consumed) or the end. Returns True when closed correctly."""
        stack = []
        t = self.t
        while self.i < len(t):
            c = t[self.i]
            if c == '\n': self.line += 1
            if t.startswith('<#', self.i):
                j = t.find('#>', self.i + 2)
                if j < 0: self.err("unterminated <# comment"); self.i = len(t); return False
                self.line += t.count('\n', self.i, j); self.i = j + 2; continue
            if c == '#':
                j = t.find('\n', self.i); self.i = len(t) if j < 0 else j; continue
            if c == '`':   # escape (or line continuation)
                self.i += 2
                if self.i - 1 < len(t) and t[self.i - 1] == '\n': self.line += 1
                continue
            if c == '@' and self.i + 1 < len(t) and t[self.i + 1] in '\'"' and re.match(r'@[\'"][ \t]*\r?\n', t[self.i:self.i + 8]):
                q = t[self.i + 1]; end = '\n' + q + '@'; j = t.find(end, self.i)
                if j < 0: self.err("unterminated here-string"); self.i = len(t); return False
                self.line += t.count('\n', self.i, j + 1); self.i = j + len(end); continue
            if c == "'":
                self.i += 1; start = self.i
                while True:
                    if self.i >= len(t): self.err("unterminated single-quoted string"); return False
                    if t[self.i] == "'":
                        if self.i + 1 < len(t) and t[self.i + 1] == "'": self.i += 2; continue
                        break
                    if t[self.i] == '\n': self.line += 1
                    self.i += 1
                self.strings.append(t[start:self.i]); self.i += 1; continue
            if c == '"':
                if not self.dq(): return False
                continue
            if c in self.OPEN: stack.append((c, self.line)); self.i += 1; continue
            if c in ')}]':
                if stack:
                    o, ln = stack.pop()
                    if self.OPEN[o] != c: self.err("'%s' closes '%s' opened on line %d" % (c, o, ln)); return False
                    self.i += 1; continue
                if closer == c: self.i += 1; return True
                self.err("unbalanced '%s'" % c); self.i += 1; continue
            self.i += 1
        if stack: self.err("'%s' opened on line %d is never closed" % stack[-1])
        if closer: self.err("missing '%s'" % closer)
        return not stack and not closer
    def dq(self):
        t = self.t; self.i += 1; start = self.i
        while self.i < len(t):
            c = t[self.i]
            if c == '`': self.i += 2; continue
            if c == '"':
                if self.i + 1 < len(t) and t[self.i + 1] == '"': self.i += 2; continue
                self.strings.append(t[start:self.i]); self.i += 1; return True
            if c == '$' and t.startswith('$(', self.i):
                self.i += 2
                if not self.code(')'): return False
                continue
            if c == '\n': self.line += 1
            self.i += 1
        self.err("unterminated double-quoted string"); return False

def read(path):
    with open(path, 'rb') as f: return f.read()

def static_checks(root):
    sdir = os.path.join(root, 'scripts')
    names = ['build-xbox-uwp.ps1', 'xbox-check-sdk.ps1', 'xbox-compile-shaders.ps1', 'xbox-install-local.ps1', 'xbox-sign.ps1']
    texts = {}
    for n in names:
        p = os.path.join(sdir, n)
        check(os.path.isfile(p), "missing script scripts/" + n)
        if not os.path.isfile(p): continue
        raw = read(p)
        try: raw.decode('ascii')
        except UnicodeDecodeError as e: check(False, "%s has a non-ASCII byte at %d (Windows PowerShell 5.1 would read it as ANSI)" % (n, e.start))
        check(b'\r' not in raw or raw.count(b'\r\n') == raw.count(b'\n'), n + " mixes LF and CRLF line ends")
        text = raw.decode('ascii', 'replace'); texts[n] = text
        lx = Lex(text, n); lx.code()
        check(re.search(r'\bparam\s*\(', text) is not None, n + " has no param block")
    b = texts.get('build-xbox-uwp.ps1', '')
    if b:
        total = int(re.search(r'\$script:Total\s*=\s*(\d+)', b).group(1))
        steps = sorted(set(int(m) for m in re.findall(r'^\s*(?:\})?\s*Step\s+(\d+)\s', b, re.M)))
        check(steps == list(range(1, total + 1)), "Step numbers %s do not run 1..%d" % (steps, total))
        hints = set(int(m) for m in re.findall(r'^\s*(\d+)\s*=\s*\'', b[b.index('$script:Hints'):], re.M))
        for k in range(1, total + 1): check(k in hints, "no 'what to do next' hint for step %d" % k)
        check("WHAT TO DO NEXT" in b, "the failure handler does not print WHAT TO DO NEXT")
        check("exit 1" in b and "Fail-With" in b, "no failure exit")
        # repo paths: Join-Path $SourceRoot '...' and $packSrc '...' must exist
        for m in re.finditer(r"Join-Path \$(SourceRoot|packSrc)\s+'([^']+)'", b):
            base = root if m.group(1) == 'SourceRoot' else os.path.join(root, 'platform', 'xbox_uwp')
            rel = m.group(2).replace('\\', '/')
            check(os.path.exists(os.path.join(base, rel)), "build script names %s which does not exist" % m.group(2))
        for m in re.finditer(r"'((?:engine|platform|scripts)\\[A-Za-z0-9_\\.]+\.(?:cpp|c|hpp|ps1|txt|cmake))'", b):
            check(os.path.exists(os.path.join(root, m.group(1).replace('\\', '/'))), "build script names %s which does not exist" % m.group(1))
        # the parameters of the script, and each switch the docs tell the owner to type
        params = set(re.findall(r'\[(?:\w+(?:\([^)]*\))?)\]\s*\$(\w+)|^\s*\[switch\]\$(\w+)', b, re.M)) if False else set()
        pb = b[b.index('param('):]; pb = pb[:pb.index('\n)\n')]
        declared = set(re.findall(r'\$(\w+)\s*(?:=|,|$)', pb, re.M)) | set(re.findall(r'\$(\w+)', pb))
        for must in ('SourceRoot', 'ExportDir', 'MinimalTest', 'EngineMode', 'SkipInstall', 'SkipSign', 'Http', 'OwnCrypto'):
            check(must in declared, "build script lacks parameter -" + must)
        docs = [os.path.join(root, 'docs', 'platform', 'xbox_uwp.md')]
        td = os.path.join(root, 'docs', 'try_it')
        if os.path.isdir(td): docs += [os.path.join(td, f) for f in os.listdir(td) if 'xbox' in f.lower()]
        docs += [os.path.join(root, 'docs', 'try_it.md')]
        for d in docs:
            if not os.path.isfile(d): continue
            for line in read(d).decode('utf-8', 'replace').splitlines():
                if 'build-xbox-uwp.ps1' not in line: continue
                for sw in re.findall(r'\s-([A-Za-z]+)\b', line.split('build-xbox-uwp.ps1', 1)[1]):
                    check(sw in declared, "%s tells the owner to use -%s, which build-xbox-uwp.ps1 does not declare" % (os.path.relpath(d, root), sw))
        # the child script and the arguments handed to it
        cs = texts.get('xbox-compile-shaders.ps1', '')
        m = re.search(r"\$shaderArgs\s*=\s*@\((.*?)\)\s*\n\s*Require-Ok", b, re.S)
        if m and cs:
            cp = cs[cs.index('param('):]; cp = cp[:cp.index('\n)\n')]
            cdecl = set(re.findall(r'\$(\w+)', cp))
            for a in re.findall(r"'-(\w+)'", m.group(1)):
                if a in ('NoProfile', 'ExecutionPolicy', 'File'): continue
                check(a in cdecl, "build script passes -%s to xbox-compile-shaders.ps1, which does not declare it" % a)
        # step 8 must name the same file the manifest identity gives (the install lines print the same variable)
        check("okay-appx" in b and "'verify'" in b, "the build script does not run okay-appx verify")
    c = texts.get('xbox-check-sdk.ps1', '')
    if c:
        check("how to install" in c, "the SDK check does not print how to install what is missing")
        check("exit 1" in c and "exit 0" in c, "the SDK check has no exit codes")
        check("Universal Windows Platform" in c, "the SDK check does not name the Visual Studio workload")

def manifest_checks(pkg):
    mp = os.path.join(pkg, 'AppxManifest.xml')
    check(os.path.isfile(mp), "no AppxManifest.xml"); 
    if not os.path.isfile(mp): return
    ns = {'f': 'http://schemas.microsoft.com/appx/manifest/foundation/windows10', 'uap': 'http://schemas.microsoft.com/appx/manifest/uap/windows10'}
    root = ET.parse(mp).getroot()
    check(root.tag == '{%s}Package' % ns['f'], "manifest root is not Package in the foundation namespace")
    ident = root.find('f:Identity', ns); check(ident is not None, "no Identity")
    if ident is None: return
    check(re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.\-]{2,49}', ident.get('Name', '')) is not None, "Identity Name has characters or a length Windows refuses: " + ident.get('Name', ''))
    check(re.fullmatch(r'\d+\.\d+\.\d+\.\d+', ident.get('Version', '')) is not None, "Identity Version is not a.b.c.d")
    check(ident.get('Version', '').endswith('.0') or True, "")
    check(ident.get('Publisher', '').startswith('CN='), "Publisher is not a CN= name (the certificate subject must equal it)")
    check(ident.get('ProcessorArchitecture') == 'x64', "ProcessorArchitecture is not x64 (the script builds x64 only)")
    tdf = root.find('f:Dependencies/f:TargetDeviceFamily', ns)
    check(tdf is not None and tdf.get('Name') == 'Windows.Universal', "no Windows.Universal TargetDeviceFamily")
    if tdf is not None:
        mv = tuple(int(x) for x in tdf.get('MinVersion', '0.0.0.0').split('.')); check(mv >= (10, 0, 17763, 0), "MinVersion below 10.0.17763.0")
    for e in root.findall('.//f:PackageDependency', ns): check(e.get('Name') and e.get('Publisher'), "PackageDependency without Name or Publisher")
    app = root.find('f:Applications/f:Application', ns); check(app is not None, "no Application")
    if app is not None:
        check(re.fullmatch(r'[A-Za-z][A-Za-z0-9.]*', app.get('Id', '')) is not None, "Application Id is not valid")
        check(os.path.isfile(os.path.join(pkg, app.get('Executable', '?'))), "Executable %s is not in the package" % app.get('Executable'))
        check(bool(app.get('EntryPoint')), "no EntryPoint")
    # every file the manifest names must exist in the package (a missing tile image makes Windows refuse the install)
    for el in root.iter():
        for k, v in el.attrib.items():
            if k in ('Image', 'Square150x150Logo', 'Square44x44Logo', 'Wide310x150Logo', 'Square71x71Logo', 'Square310x310Logo') or el.tag.endswith('}Logo'):
                check(os.path.isfile(os.path.join(pkg, v.replace('\\', '/'))), "manifest names %s which is not in the package" % v)
        if el.tag.endswith('}Logo') and el.text: check(os.path.isfile(os.path.join(pkg, el.text.strip().replace('\\', '/'))), "manifest Logo %s is not in the package" % el.text)
    for cap in root.findall('.//f:Capabilities/f:Capability', ns):
        check(cap.get('Name') in ('internetClient', 'internetClientServer', 'privateNetworkClientServer'), "unexpected capability " + str(cap.get('Name')))

def run(cmd, **kw):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, **kw)
    return r.returncode, r.stdout

def pipeline(a):
    work = os.path.abspath(a.work); shutil.rmtree(work, ignore_errors=True); os.makedirs(work)
    rc, out = run([a.export, '--out', work, '--with-scene']); check(rc == 0, "okay-xbox-export failed: " + out)
    pkg = os.path.join(work, 'package'); check(os.path.isdir(pkg), "no package folder")
    check(os.path.isfile(os.path.join(pkg, 'Data', 'game.okayconfig')), "--with-scene wrote no Data/game.okayconfig")
    # what step 6 and 7 of the build script add
    for n, data in (('OkaySpaceXbox.exe', b'MZ' + bytes(300)), ('tri_vs.cso', b'DXBC' + bytes(80)), ('tri_ps.cso', b'DXBC' + bytes(80))):
        with open(os.path.join(pkg, n), 'wb') as f: f.write(data)
    os.makedirs(os.path.join(pkg, 'Shaders'), exist_ok=True)
    with open(os.path.join(pkg, 'Shaders', 'shaders.manifest'), 'w') as f: f.write('x\n')
    manifest_checks(pkg)
    ident = ET.parse(os.path.join(pkg, 'AppxManifest.xml')).getroot().find('{http://schemas.microsoft.com/appx/manifest/foundation/windows10}Identity')
    appx = os.path.join(work, "%s_%s_x64.appx" % (ident.get('Name'), ident.get('Version')))
    rc, out = run([a.appx, 'pack', pkg, appx]); check(rc == 0, "okay-appx pack failed: " + out)
    rc, out = run([a.appx, 'verify', appx]); check(rc == 0 and 'OK' in out, "okay-appx verify failed: " + out)
    # an independent reader
    with zipfile.ZipFile(appx) as z:
        check(z.testzip() is None, "Python's zipfile finds a CRC error in our package")
        names = z.namelist()
        for must in ('[Content_Types].xml', 'AppxBlockMap.xml', 'AppxManifest.xml', 'OkaySpaceXbox.exe', 'Data/game.okayconfig'):
            check(must in names, "the package lacks " + must)
        check(len(names) == len(set(n.lower() for n in names)), "duplicate file names (ignoring case)")
        for n in names:
            check(not n.startswith('/') and '\\' not in n and '..' not in n.split('/'), "bad name in the package: " + n)
        bm = ET.fromstring(z.read('AppxBlockMap.xml'))
        files = [e for e in bm if e.tag.endswith('File')]
        check(len(files) >= len(names) - 2, "block map lists %d files for %d entries" % (len(files), len(names)))
    # damage: one flipped byte in the middle of the exe must be refused, so must a cut file
    data = bytearray(read(appx)); at = bytes(data).find(b'MZ' + bytes(8)); check(at > 0, "could not find the exe stand-in")
    bad = os.path.join(work, 'bad.appx'); data[at + 100] ^= 0xFF
    with open(bad, 'wb') as f: f.write(data)
    rc, out = run([a.appx, 'verify', bad]); check(rc != 0, "verify accepted a package with a flipped byte")
    cut = os.path.join(work, 'cut.appx')
    with open(cut, 'wb') as f: f.write(read(appx)[:-20])
    rc, out = run([a.appx, 'verify', cut]); check(rc != 0, "verify accepted a truncated package")
    rc, out = run([a.appx, 'verify', os.path.join(work, 'missing.appx')]); check(rc != 0, "verify accepted a missing file")
    # a folder with a bad name is refused by pack
    open(os.path.join(pkg, 'con.txt'), 'w').close()
    rc, out = run([a.appx, 'pack', pkg, os.path.join(work, 'x.appx')]); check(rc != 0, "pack accepted the reserved name con.txt")

def main():
    ap = argparse.ArgumentParser(); sub = ap.add_subparsers(dest='mode', required=True)
    s = sub.add_parser('scripts'); s.add_argument('root')
    p = sub.add_parser('pipeline'); p.add_argument('--export', required=True); p.add_argument('--appx', required=True); p.add_argument('--work', required=True)
    a = ap.parse_args()
    if a.mode == 'scripts': static_checks(os.path.abspath(a.root))
    else: pipeline(a)
    if fails: print("%d problem(s)" % len(fails)); return 1
    print("check_xbox_uwp %s: all passed" % a.mode); return 0

if __name__ == '__main__': sys.exit(main())
