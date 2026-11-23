#!/usr/bin/env python3
"""scripts/check_modules.py on a small made-up tree (a fixture written to a temporary folder): allowed and forbidden
includes, the shrink-only baseline, manifest problems and the umbrella header. Usage: test_check_modules.py <check_modules.py>"""
import os, subprocess, sys, tempfile

SCRIPT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', '..', 'scripts', 'check_modules.py')
failures = 0


def check(cond, what):
    global failures
    if not cond:
        failures += 1
        print('FAIL: ' + what)


def write(root, rel, text):
    p = os.path.join(root, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'w', encoding='utf-8') as f:
        f.write(text)


def run(root, *args):
    p = subprocess.run([sys.executable, '-I', SCRIPT, '--root', root, *args], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True)
    return p.returncode, p.stdout


with tempfile.TemporaryDirectory() as root:
    os.makedirs(os.path.join(root, 'scripts'))
    write(root, 'engine/modules/core/module.okaymodule', 'name=core\ncore=1\n')
    write(root, 'engine/modules/alpha/module.okaymodule', 'name=alpha\nrequires=core\nsources=include/okay/A/* src/A/*\n')
    write(root, 'engine/modules/beta/module.okaymodule', 'name=beta\nrequires=alpha\nsources=include/okay/B/*\n')
    write(root, 'engine/include/okay/Core/Base.hpp', '#pragma once\n')
    write(root, 'engine/include/okay/A/a.hpp', '#pragma once\n#include "okay/Core/Base.hpp"\n')    # module -> core: allowed
    write(root, 'engine/include/okay/B/b.hpp', '#pragma once\n#include "okay/A/a.hpp"\n')         # beta requires alpha: allowed
    write(root, 'engine/src/A/a.cpp', '#include "okay/A/a.hpp"\n')

    code, out = run(root)
    check(code == 0, 'a clean tree passes: ' + out)
    check('new violations: 0' in out, 'clean tree counts nothing: ' + out)

    # alpha includes beta, which alpha does not require; core includes a module
    write(root, 'engine/src/A/a.cpp', '#include "okay/A/a.hpp"\n#include "okay/B/b.hpp"\n')
    write(root, 'engine/include/okay/Core/Base.hpp', '#pragma once\n#include "okay/A/a.hpp"\n')
    code, out = run(root)
    check(code == 1, 'a forbidden include fails: ' + out)
    check('src/A/a.cpp' in out and 'beta is not in alpha.requires' in out, 'the message names the file and the module: ' + out)
    check('core (include/okay/Core/Base.hpp)' in out, 'core including a module is forbidden too: ' + out)

    # the baseline takes today's list; then the check passes and reports them as known
    code, out = run(root, '--update-baseline')
    check(code == 0 and 'baseline written: 2 entries' in out, 'baseline written: ' + out)
    code, out = run(root)
    check(code == 0 and 'known violations: 2' in out, 'known violations pass: ' + out)

    # a new one still fails
    write(root, 'engine/include/okay/A/more.hpp', '#pragma once\n#include "okay/B/b.hpp"\n')
    code, out = run(root)
    check(code == 1 and 'include/okay/A/more.hpp' in out, 'a new violation fails even with a baseline: ' + out)
    os.remove(os.path.join(root, 'engine/include/okay/A/more.hpp'))

    # fixing one leaves a stale baseline line, which is reported (the baseline can only shrink)
    write(root, 'engine/src/A/a.cpp', '#include "okay/A/a.hpp"\n')
    code, out = run(root)
    check(code == 0 and 'no longer apply' in out, 'a fixed violation is reported as a stale baseline entry: ' + out)

    # manifest problems: an unknown requirement, a cycle, a pattern that matches nothing
    write(root, 'engine/modules/gamma/module.okaymodule', 'name=gamma\nrequires=nothere\nsources=include/okay/G/*\n')
    write(root, 'engine/modules/alpha/module.okaymodule', 'name=alpha\nrequires=beta\nsources=include/okay/A/* src/A/*\n')
    code, out = run(root)
    check(code == 1, 'manifest problems fail: ' + out)
    check("requires unknown module 'nothere'" in out, 'unknown requirement named: ' + out)
    check('require each other' in out or 'cycle' in out, 'cycle named: ' + out)
    check("'include/okay/G/*' matches no file" in out, 'empty pattern named: ' + out)
    os.remove(os.path.join(root, 'engine/modules/gamma/module.okaymodule'))
    write(root, 'engine/modules/alpha/module.okaymodule', 'name=alpha\nrequires=core\nsources=include/okay/A/* src/A/*\n')

    # engine code must not include the umbrella header
    write(root, 'engine/src/A/a.cpp', '#include "Okay.hpp"\n')
    code, out = run(root)
    check(code == 1 and 'umbrella Okay.hpp' in out, 'the umbrella header is refused: ' + out)

print('check_modules fixture: ' + ('OK' if failures == 0 else '%d failure(s)' % failures))
sys.exit(1 if failures else 0)
