#!/usr/bin/env python3
"""Do the Inspector menu tests really catch the bugs they are about? Put each bug back, one at a time, and see the test go red.

For every fix of the Inspector menus review (docs/inspector_menus.md) this script makes a temporary copy of the one source line that
carries the fix, writes the OLD behaviour (or removes the fix) into the copy, builds the matching test against the copy, runs it and
reports "RED" (the test caught it) or "GREEN (!)" (the test did not notice: the test is too weak). It never edits the repository.

  python3 scripts/inspector_menus_mutation_check.py                       header-only rules (g++ only, a few seconds)
  python3 scripts/inspector_menus_mutation_check.py --imgui <dir>         + the window-less Dear ImGui test (<dir> = an ImGui checkout)
  python3 scripts/inspector_menus_mutation_check.py --build <build dir>   + the engine-linked tests (<build dir> = a CMake build of the tests
                                                                           with libokay_engine.a already built)
  --only <text>   run only the mutations whose name contains <text>

Exit code 0 when every mutation was caught, 1 otherwise (or when a mutation no longer applies because the source changed).
"""
import argparse, os, shutil, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EDITOR = os.path.join(ROOT, 'editor')
TESTS = os.path.join(ROOT, 'tests')
ENGINE_DEFS = ['-DOKAY_HAVE_SODIUM', '-DOKAY_NET_CRYPTO', '-DOKAY_PLATFORM_HAVE_OWN', '-DOKAY_PLATFORM_HAVE_SDL2', '-DOKAY_PLATFORM_X11_HEADERS']

MODEL = 'editor/InspectorModel.cpp'
CLICK = 'editor/InspectorClickRules.hpp'
CLIP = 'editor/InspectorClipboardRules.hpp'
CTX = 'editor/InspectorContextModel.hpp'
IDS = 'editor/ComponentIdScope.hpp'
TABLE = 'editor/AddComponentRules.hpp'

# (name, test, [(file, old text, new text)])
HEADER_ONLY = [
    ('3a a field menu does not claim the click (the empty-space menu replaces it)', 'test_inspector_click_rules',
     [(CLICK, "        if (!Truth(hasMenu)) return false;\n        m_claimFrame = frame;\n        return true;", "        if (!Truth(hasMenu)) return false;\n        return true;")]),
    ('3b a field row is only its last widget (label and gap not covered)', 'test_inspector_click_rules',
     [(CLICK, "    r.x0 = contentLeft;\n    r.x1 = contentRight;", "    r.x0 = contentRight - 200;\n    r.x1 = contentRight;")]),
    ('3c the empty-space menu ignores the claim', 'test_inspector_click_rules',
     [(CLICK, "return click.released && windowHovered && !anyItemHovered && !Claimed(frame);", "return click.released && windowHovered && !anyItemHovered;")]),
    ('3d two fields can both open in one frame', 'test_inspector_click_rules',
     [(CLICK, "if (!click.released || !hot || m_claimFrame == frame) return false;", "if (!click.released || !hot) return false;")]),
    ('3e the gap between two rows belongs to nobody', 'test_inspector_click_rules',
     [(CLICK, "    const float lastTopPadded = lastTop - pad;\n    r.y0 = rowTop < lastTopPadded ? rowTop : lastTopPadded;\n    r.y1 = lastBottom + pad;", "    r.y0 = lastTop;\n    r.y1 = lastBottom;")]),
    ('4a any camera in the selection locks Reset Scale (the old rule)', 'test_inspector_context_model',
     [(CTX, "    st.scaleLocked = !objs.empty() && scalable == 0;", "    st.scaleLocked = st.cameraCount > 0;")]),
    ('4b a camera counts when asking "is the scale at its default"', 'test_inspector_context_model',
     [(CTX, "        if (o.camera) { ++st.cameraCount; continue; }\n        ++scalable;", "        if (o.camera) ++st.cameraCount;\n        ++scalable;")]),
    ('5a Paste As New names the objects that have the right-clicked kind', 'test_inspector_context_model',
     [(CTX, 'Many(s.clipType.empty() ? std::string("Paste As New Component") : "Paste As New " + s.clipType, s.pasteNewObjects));\n        pn.tip',
            'Many(s.clipType.empty() ? std::string("Paste As New Component") : "Paste As New " + s.clipType, s.selectionCount));\n        pn.tip')]),
    ('5b the empty-space menu names all selected objects', 'test_inspector_context_model',
     [(CTX, 'Many(s.clipType.empty() ? std::string("Paste As New Component") : "Paste As New " + s.clipType, s.pasteNewObjects));\n    pn.tip',
            'Many(s.clipType.empty() ? std::string("Paste As New Component") : "Paste As New " + s.clipType, s.selectionCount));\n    pn.tip')]),
    ('1e Duplicate is enabled for a kind that is only allowed once', 'test_inspector_context_model',
     [(CTX, "        else if (!s.canAddAnother)\n            m.push_back(Off(dup, s.noAnotherReason.empty()", "        else if (false)\n            m.push_back(Off(dup, s.noAnotherReason.empty()")]),
    ('2d Duplicate is enabled for a copy that would not be the same', 'test_inspector_context_model',
     [(CTX, "        else if (!s.copyExact)\n            m.push_back(Off(dup,", "        else if (false)\n            m.push_back(Off(dup,")]),
    ('2e the Copy Values tip says it copies every value', 'test_inspector_context_model',
     [(CTX, 'cp.tip = "Copy the values of this component that its property list holds (what scripts and Custom Actions can read and write). "', 'cp.tip = "Copy every value of this component. "')]),
    ('6a the clipboard is not capped', 'test_inspector_clipboard_rules',
     [(CLIP, "    c.tooLong = n > kMaxClipboardChars;\n    c.text.assign(raw, c.tooLong ? kMaxClipboardChars : n);", "    c.tooLong = false;\n    c.text.assign(raw);")]),
    ('6b several lines are accepted for a one-line field', 'test_inspector_clipboard_rules',
     [(CLIP, "            if (c == '\\n' || c == '\\r') {", "            if (false) {")]),
    ('6c ".." is allowed in a pasted path', 'test_inspector_clipboard_rules',
     [(CLIP, "        if (j - i == 2 && p[i] == '.' && p[i + 1] == '.') {", "        if (false) {")]),
    ('6d network paths are allowed', 'test_inspector_clipboard_rules',
     [(CLIP, "    if (p.size() >= 2 && p[0] == '/' && p[1] == '/') {", "    if (false) {")]),
    ('6e absolute paths outside the project are allowed', 'test_inspector_clipboard_rules',
     [(CLIP, "    if (!inside) {\n        v.check = PathCheck::OutsideProject;", "    if (false) {\n        v.check = PathCheck::OutsideProject;")]),
    ('6f the preview is not shortened', 'test_inspector_clipboard_rules',
     [(CLIP, '            if (chars == maxChars) { out += "..."; return out; }', '            if (false) { out += "..."; return out; }')]),
    ('6g the Paste Value row ignores the reason it was refused', 'test_inspector_context_model',
     [(CTX, "        m.push_back(Off(pv, !s.clipRefusal.empty() ? s.clipRefusal\n                                                   : \"The clipboard does not hold a value this field can take. Use Copy Value on a field of the same kind.\"));",
            "        m.push_back(Off(pv, \"The clipboard does not hold a value this field can take. Use Copy Value on a field of the same kind.\"));")]),
    ('6h Paste Value stays on when the field already holds the value (an empty undo step)', 'test_inspector_context_model',
     [(CTX, "    else if (s.clipIsSame) m.push_back(Off(pv, \"The field already holds the copied value.\"));\n", "")]),
]

IMGUI = [
    ('3f ImGui: a field no longer claims the click (the old behaviour)', 'test_inspector_imgui',
     [(CLICK, "        if (!Truth(hasMenu)) return false;\n        m_claimFrame = frame;\n        return true;", "        if (!Truth(hasMenu)) return false;\n        return true;")]),
    ('3g ImGui: the empty-space menu ignores the claim', 'test_inspector_imgui',
     [(CLICK, "return click.released && windowHovered && !anyItemHovered && !Claimed(frame);", "return click.released && windowHovered && !anyItemHovered;")]),
    ('1b ImGui: the component id scope pushes no id (two cards share widget ids)', 'test_inspector_imgui',
     [(IDS, "explicit ComponentIdScope(const void* component) { ImGui::PushID(component); }\n    ~ComponentIdScope() { ImGui::PopID(); }", "explicit ComponentIdScope(const void*) {}\n    ~ComponentIdScope() {}")]),
]

ENGINE = [
    ('1a only a project rule counts as "only one per object" (the old behaviour: nothing is refused)', 'test_inspector_menu_actions',
     [(MODEL, "    return IsOnePerObject(info.type) || ComponentRules::IsUnique(info.type);", "    return ComponentRules::IsUnique(info.type);")]),
    ('1b Paste As New ignores "only one per object"', 'test_inspector_menu_actions',
     [(MODEL, "    if (OnePerObject(*info)) {\n        bool has = HasOnePerObjectKind(go, info->type);", "    if (false) {\n        bool has = HasOnePerObjectKind(go, info->type);")]),
    ('1c the table loses AudioSource (a browser row would no longer match it)', 'test_add_component_rules', [(TABLE, "X(AudioSource) ", "")]),
    ('1d the table gains kinds that no browser row uses', 'test_add_component_rules', [(TABLE, "X(AimIK) ", "X(AimIK) X(ParticleSystem) X(Volume) ")]),
    ('2a CheckCopyIsExact always says "exact"', 'test_inspector_menu_actions', [(MODEL, "    if (addedSorted == ownSorted) return r;", "    return r;")]),
    ('2b Paste As New ignores a clip that is not exact', 'test_inspector_menu_actions',
     [(MODEL, "    if (!clip.exact) {\n        if (why) *why = \"A new \"", "    if (false) {\n        if (why) *why = \"A new \"")]),
    ('2c a writer that saves one record per object counts as exact', 'test_inspector_menu_actions',
     [(MODEL, "    if (added.empty()) {\n        r.exact = false;\n        r.note = \"the scene file keeps only one \"", "    if (false) {\n        r.exact = false;\n        r.note = \"the scene file keeps only one \"")]),
    ('4c ScaleIsLocked is always false (cameras are scaled)', 'test_inspector_menu_actions',
     [(MODEL, "bool ScaleIsLocked(const GameObject& go) { return go.GetComponent<Camera>() != nullptr; }", "bool ScaleIsLocked(const GameObject& go) { (void)go; return false; }")]),
    ('4d WriteTargets does not leave cameras out of a scale', 'test_inspector_menu_actions',
     [(MODEL, "        if (scale && ScaleIsLocked(*g)) { ++left; continue; }\n", "")]),
    ('4e Paste All Component Values scales a camera', 'test_inspector_menu_actions',
     [(MODEL, "        if (lockScale && e.component == \"Transform\" && e.property == \"scale\") continue;\n", "")]),
    ('5c PasteAsNewTargets counts every object', 'test_inspector_menu_actions',
     [(MODEL, "        if (CanPasteAsNew(*g, clip, &why)) ++n;", "        if (true) ++n;")]),
    ('6i Paste Value accepts nan and infinity (no registry check)', 'test_inspector_menu_actions',
     [(MODEL, "    if (!desc.Normalize(parsed, norm)) { r.reason = RefusalFor(desc, parsed); return r; }", "    norm = parsed;")]),
    ('6j Paste Value accepts any file path', 'test_inspector_menu_actions',
     [(MODEL, "    if (desc.type == PropertyType::AssetPath) {\n        const inspectorctx::PathVerdict", "    if (false) {\n        const inspectorctx::PathVerdict")]),
    ('6k Paste Value does not look at the size or the lines', 'test_inspector_menu_actions',
     [(MODEL, "    if (gate.gate != inspectorctx::PasteGate::Ok) { r.reason = gate.reason; return r; }", "    (void)gate;")]),
    ('6l Paste Value never says "the field already holds it"', 'test_inspector_menu_actions',
     [(MODEL, "    r.same = current.have > 0 && !current.mixed && current.value == norm;", "    r.same = false;")]),
    ('7a Reset Rotation is grey for turns up to 0.16 degrees (|w| > 0.999999)', 'test_inspector_menu_actions',
     [(MODEL, "    if (std::fabs(q.x) <= 1e-7f && std::fabs(q.y) <= 1e-7f && std::fabs(q.z) <= 1e-7f && std::fabs(std::fabs(q.w) - 1.0f) <= 1e-6f) at |= kTransformRotation;",
              "    if (std::fabs(q.w) > 0.999999f) at |= kTransformRotation;")]),
]


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def mutated_dir(edits, work):
    """Write the edited copies into `work` (flat, by file name). Returns False when an edit does not apply any more."""
    sources = {}
    for f, old, new in edits:
        if f not in sources:
            with open(os.path.join(ROOT, f), encoding='utf-8') as fh:
                sources[f] = fh.read()
        if old not in sources[f]:
            return False
        sources[f] = sources[f].replace(old, new, 1)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    for f, text in sources.items():
        with open(os.path.join(work, os.path.basename(f)), 'w', encoding='utf-8') as fh:
            fh.write(text)
    return True


def summary(proc):
    out = proc.stdout + proc.stderr
    checks = [l for l in out.splitlines() if 'checks,' in l]
    fails = [l.strip() for l in out.splitlines() if 'FAIL' in l][:1]
    return (checks[-1] if checks else out[-120:].replace('\n', ' ')) + ('   e.g. ' + fails[0][:100] if fails else '')


def header_only(test, edits, work):
    if not mutated_dir(edits, work):
        return None
    exe = os.path.join(work, 't')
    c = run(['g++', '-std=c++17', '-O0', '-w', '-I', work, '-I', EDITOR, '-I', TESTS, os.path.join(TESTS, test + '.cpp'), '-o', exe])
    if c.returncode:
        return ('COMPILE ERROR', c.stderr[:200].replace('\n', ' '))
    r = run([exe])
    return ('RED' if r.returncode else 'GREEN (!)', summary(r))


def with_imgui(test, edits, work, imgui, objs):
    if not mutated_dir(edits, work):
        return None
    exe = os.path.join(work, 't')
    c = run(['g++', '-std=c++17', '-O0', '-w', '-I', work, '-I', EDITOR, '-I', TESTS, '-I', imgui, os.path.join(TESTS, test + '.cpp')] + objs + ['-o', exe])
    if c.returncode:
        return ('COMPILE ERROR', c.stderr[:200].replace('\n', ' '))
    r = run([exe])
    return ('RED' if r.returncode else 'GREEN (!)', summary(r))


def with_engine(test, edits, work, build):
    if not mutated_dir(edits, work):
        return None
    libs = [build + '/engine/libokay_engine.a', build + '/engine/libokay_crypto.a', '-lsodium', build + '/engine/libokay_platform.a', '/usr/lib/x86_64-linux-gnu/libSDL2.so', '-ldl']
    flags = ENGINE_DEFS + ['-I', work, '-I', EDITOR, '-I', os.path.join(ROOT, 'engine', 'include'), '-isystem', '/usr/include/SDL2', '-O0', '-DNDEBUG', '-std=c++17', '-w',
                           '-DOKED_EDITOR_DIR="%s"' % EDITOR]
    jobs = [(os.path.join(TESTS, test + '.cpp'), os.path.join(work, 'test.o'))]
    if test == 'test_inspector_menu_actions':   # this test is built together with InspectorModel.cpp (the mutated copy when that is what changed)
        model = os.path.join(work, 'InspectorModel.cpp')
        jobs.append((model if os.path.exists(model) else os.path.join(EDITOR, 'InspectorModel.cpp'), os.path.join(work, 'model.o')))
    objs = []
    for src, out in jobs:
        c = run(['nice', '-n', '10', 'g++'] + flags + ['-c', src, '-o', out])
        if c.returncode:
            return ('COMPILE ERROR', c.stderr[:200].replace('\n', ' '))
        objs.append(out)
    exe = os.path.join(work, 't')
    link = run(['g++', '-O0'] + objs + ['-o', exe] + libs)
    if link.returncode:
        return ('LINK ERROR', link.stderr[:200].replace('\n', ' '))
    r = run([exe], cwd=ROOT)
    return ('RED' if r.returncode else 'GREEN (!)', summary(r))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--imgui', help='a Dear ImGui checkout (the one the editor builds against)')
    ap.add_argument('--build', help='a CMake build directory of the tests, with engine/libokay_engine.a built')
    ap.add_argument('--only', default='', help='run only mutations whose name contains this text')
    args = ap.parse_args()
    results = []
    with tempfile.TemporaryDirectory(prefix='inspector_mutations_') as tmp:
        work = os.path.join(tmp, 'm')
        for name, test, edits in HEADER_ONLY:
            if args.only in name:
                results.append((name, test, header_only(test, edits, work)))
        if args.imgui:
            objs = []
            for f in ('imgui', 'imgui_draw', 'imgui_tables', 'imgui_widgets'):
                o = os.path.join(tmp, f + '.o')
                c = run(['g++', '-std=c++17', '-O0', '-w', '-c', os.path.join(args.imgui, f + '.cpp'), '-o', o])
                if c.returncode:
                    print('cannot compile', f, c.stderr[:200]); return 2
                objs.append(o)
            for name, test, edits in IMGUI:
                if args.only in name:
                    results.append((name, test, with_imgui(test, edits, work, args.imgui, objs)))
        if args.build:
            for name, test, edits in ENGINE:
                if args.only in name:
                    t0 = time.time()
                    res = with_engine(test, edits, work, os.path.abspath(args.build))
                    results.append((name, test, (res[0], '%s   [%ds]' % (res[1], time.time() - t0)) if res else None))
    width = max([len(n) for n, _, _ in results] or [0])
    bad = 0
    for name, test, res in results:
        if res is None:
            print(name.ljust(width), '| THE MUTATION NO LONGER APPLIES (the source changed): update this script'); bad += 1
        else:
            print(name.ljust(width), '|', res[0].ljust(7), '|', test, '|', res[1])
            if res[0] != 'RED':
                bad += 1
    print('\n%d mutations, %d caught, %d not' % (len(results), len(results) - bad, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
