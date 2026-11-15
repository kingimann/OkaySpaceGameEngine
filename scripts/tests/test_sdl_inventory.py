#!/usr/bin/env python3
"""Unit tests for scripts/sdl_inventory.py: the counting rules the SDL inventory page rests on.

They feed the script small made-up sources (through a stand-in for `git show`), so they do not depend on the real
repository. Run: python3 scripts/tests/test_sdl_inventory.py   (also ctest: sdl_inventory_script)"""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import sdl_inventory as S


def scan(src, path="launcher/x.cpp"):
    """Run scan_file on `src` as if `git show` returned it."""
    real = S.git
    S.git = lambda *a, **k: src.encode("utf-8")
    try:
        return S.scan_file("ref", path)
    finally:
        S.git = real


class SplitCode(unittest.TestCase):
    def test_comments_and_strings_are_removed(self):
        src = 'int a; // SDL_Init(0)\n/* SDL_Quit();\n   SDL_Delay(1); */ int b;\nconst char* s = "SDL_GetTicks()";\nchar c = \'S\';\n'
        nocomment, code = S.split_code(src)
        for word in ("SDL_Init", "SDL_Quit", "SDL_Delay"):
            self.assertNotIn(word, nocomment)
            self.assertNotIn(word, code)
        self.assertIn("SDL_GetTicks", nocomment)      # a string stays in the comment-free text (include paths live there)
        self.assertNotIn("SDL_GetTicks", code)         # but not in the code text
        self.assertEqual(src.count("\n"), code.count("\n"))   # line numbers stay right

    def test_raw_string(self):
        _, code = S.split_code('auto s = R"x(SDL_Quit(); " )x"; SDL_Init(0);')
        self.assertNotIn("SDL_Quit", code)
        self.assertIn("SDL_Init", code)


class Classify(unittest.TestCase):
    def test_known_symbols(self):
        want = {
            "SDL_LockMutex": "t.mutex", "SDL_CreateThread": "t.thread", "SDL_AtomicSet": "t.atomic", "SDL_atomic_t": "t.atomic",
            "SDL_GetTicks": "timers", "SDL_GetTicks64": "timers", "SDLK_a": "kb.keys", "SDL_SCANCODE_RETURN": "kb.keys",
            "Uint8": "z.typedef", "Sint16": "z.typedef", "SDL_RenderGeometry": "r.draw", "SDL_RenderGeometryRaw": "r.rare",
            "SDL_GetWindowSize": "win.geometry", "SDL_GetWindowID": "win.geometry", "SDL_GetWindowDisplayMode": "win.refresh",
            "SDL_GetWindowDisplayIndex": "win.displays", "SDL_FlashWindow": "win.flash", "SDL_GetGlobalMouseState": "m.global",
            "SDL_GetRelativeMouseMode": "m.relget", "SDL_SetRelativeMouseMode": "m.relative", "SDL_PumpEvents": "ev.pump",
            "SDL_PollEvent": "ev.loop", "SDL_Event": "ev.loop", "SDL_EventState": "ev.filter", "ImGui_ImplSDL2_NewFrame": "i.platform",
            "ImGui_ImplSDLRenderer2_Init": "i.render", "SDL_LoadWAV": "f.wav", "AUDIO_S16SYS": "f.wav", "SDL_sinf": "z.math",
            "SDL_QUIT": "ev.window", "SDL_Quit": "x.init", "SDL_WINDOW_MINIMIZED": "win.state", "SDL_WINDOW_INPUT_FOCUS": "win.state.have",
            "SDL_GL_GetCurrentContext": "gl.cur", "SDL_GL_MakeCurrent": "gl.ctx", "SDL_HINT_RENDER_DRIVER": "r.hint",
            "SDL_HINT_WINDOWS_DPI_AWARENESS": "win.dpi", "SDL_TEXTEDITING": "kb.ime", "SDL_TEXTINPUT": "kb.text",
        }
        for sym, cid in want.items():
            self.assertEqual(S.classify(sym), cid, sym)

    def test_unknown_symbol_is_reported_not_hidden(self):
        self.assertEqual(S.classify("SDL_SomethingNew"), "zz.unclassified")

    def test_table_is_consistent(self):
        ids = [c[0] for c in S.CAPS]
        self.assertEqual(len(ids), len(set(ids)), "duplicate capability id")
        families = {f for f, _ in S.FAMILIES}
        for c in S.CAPS:
            self.assertIn(c[1], families, c[0])
            self.assertIn(c[2], ("HAVE", "PARTIAL", "GAP", "OTHER"), c[0])

    def test_no_prefix_swallowing(self):
        # a name that only starts like a covered one must not take its row (the alternation is anchored as a whole)
        self.assertEqual(S.classify("SDL_RenderGeometryRaw"), "r.rare")
        self.assertEqual(S.classify("SDL_GetWindowSizeInPixels"), "zz.unclassified")


class ScanFile(unittest.TestCase):
    def test_calls_and_refs(self):
        fi = scan('SDL_Window* w = SDL_CreateWindow("t", 0, 0, 1, 1, SDL_WINDOW_RESIZABLE);\nSDL_DestroyWindow(w);\n')
        self.assertEqual(fi.calls, 2)               # the two calls
        self.assertEqual(fi.refs, 4)                # + SDL_Window and SDL_WINDOW_RESIZABLE
        self.assertEqual(fi.counts[("win.create", "call")], 2)

    def test_comments_strings_and_local_helpers_are_not_counted(self):
        fi = scan('// SDL_Init(0)\nconst char* s = "SDL_Quit()";\nstatic void SDL_DelayCompat() {}\n')
        self.assertEqual(fi.refs, 0)

    def test_include_counted_once(self):
        fi = scan('#include <SDL.h>\n#include <SDL_syswm.h>\n#include "okay/UI/UIPainterSDL.hpp"\n#include <backends/imgui_impl_sdl2.h>\n')
        self.assertEqual(fi.counts[("z.include", "ref")], 1)       # only SDL.h; our own UIPainterSDL.hpp is not an SDL header
        self.assertEqual(fi.counts[("win.syswm", "ref")], 1)       # SDL_syswm.h belongs to the tray-hook row, not counted twice
        self.assertEqual(fi.counts[("i.platform", "ref")], 1)

    def test_window_flags_call_goes_by_the_flags_asked(self):
        self.assertEqual(scan("if (SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED) return;").counts[("win.state", "call")], 1)
        self.assertEqual(scan("if (SDL_GetWindowFlags(w) & SDL_WINDOW_INPUT_FOCUS) return;").counts[("win.state.have", "call")], 1)
        # the flags in the next statement count too (Uint32 f = SDL_GetWindowFlags(w); bool b = f & ...;)
        two = scan("Uint32 f = SDL_GetWindowFlags(w);\nbool b = (f & (SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS)) != 0;\n")
        self.assertEqual(two.counts[("win.state", "call")], 1)
        mixed = scan("if ((SDL_GetWindowFlags(w) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_INPUT_FOCUS)) != SDL_WINDOW_INPUT_FOCUS) f();")
        self.assertEqual(mixed.counts[("win.state", "call")], 1)

    def test_hint_call_goes_by_the_hint(self):
        self.assertEqual(scan('SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");').counts[("win.dpi", "call")], 1)
        self.assertEqual(scan('SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");').counts[("r.hint", "call")], 1)
        self.assertEqual(scan('SDL_SetHint(SDL_HINT_GRAB_KEYBOARD, "1");').counts[("x.hint", "call")], 1)

    def test_group_of(self):
        self.assertEqual(S.group_of("engine/src/Platform/SdlBackend.cpp"), "seam")
        self.assertEqual(S.group_of("tests/test_x.cpp"), "tests")
        self.assertEqual(S.group_of("okayui/tests/test_x.cpp"), "tests")
        self.assertEqual(S.group_of("launcher/main.cpp"), "launcher")
        self.assertEqual(S.group_of("shared/Splash.hpp"), "shared")


if __name__ == "__main__":
    unittest.main(verbosity=1)
