#include "test_framework.hpp"
#include "okay/Debug/DrawCallRecorder.hpp"

#include <string>
#include <vector>
#ifndef _WIN32
#include <thread>
#endif

using namespace okay;

static bool Balanced(const std::string& s) {
    int curly = 0, square = 0; bool inStr = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; continue; }
        if (c == '"') inStr = true;
        else if (c == '{') ++curly; else if (c == '}') { if (--curly < 0) return false; }
        else if (c == '[') ++square; else if (c == ']') { if (--square < 0) return false; }
    }
    return !inStr && curly == 0 && square == 0;
}
static int CountOf(const std::string& s, const std::string& pat) {
    int n = 0; for (size_t p = s.find(pat); p != std::string::npos; p = s.find(pat, p + pat.size())) ++n; return n;
}

static DrawCallRecord Call(const std::string& name, const std::string& pass, const std::string& mat, const std::string& mesh,
                           const std::string& shader, std::uint64_t tris, DrawCallKind kind = DrawCallKind::DrawIndexed) {
    DrawCallRecord c;
    c.kind = kind; c.name = name; c.pass = pass; c.material = mat; c.mesh = mesh;
    c.objectPath = "World/" + name;
    c.shaderState.shader = shader;
    c.shaderState.blend = "opaque";
    c.viewport.width = 1280; c.viewport.height = 720;
    c.triangleCount = tris; c.indexCount = (std::uint32_t)tris * 3; c.vertexCount = (std::uint32_t)tris * 2;
    return c;
}

// A tiny frame: shadows (2 draws), opaque (4 draws, 3 materials), one compute dispatch, a post pass on a target.
static void CaptureSampleFrame(DrawCallRecorder& r, std::uint64_t frame) {
    r.BeginFrame(frame);
    r.Record(Call("Crate", "Shadows", "", "cube", "depth", 12));
    r.Record(Call("Rock", "Shadows", "", "rock", "depth", 500));
    r.Record(Call("Crate", "Opaque", "wood", "cube", "lit", 12));
    r.Record(Call("Crate2", "Opaque", "wood", "cube", "lit", 12));
    r.Record(Call("Rock", "Opaque", "stone", "rock", "lit", 500));
    DrawCallRecord glass = Call("Window", "Opaque", "glass", "quad", "lit_alpha", 2);
    glass.shaderState.blend = "alpha"; glass.shaderState.depthWrite = false;
    glass.shaderState.extra.push_back({"u_opacity", "0.4"});
    r.Record(glass);
    DrawCallRecord cs = Call("Cull", "Compute", "", "", "", 0, DrawCallKind::Dispatch);
    cs.groupsX = 8; cs.groupsY = 8; cs.groupsZ = 1;
    r.Record(cs);
    DrawCallRecord post = Call("Bloom", "Post", "bloom", "fullscreen", "post", 2);
    post.renderTarget = "bloom_half";
    r.Record(post);
    r.EndFrame();
}

int main() {
    RUN_SUITE("debug_draw_calls");

    // ---- capture next frame -----------------------------------------------------
    {
        DrawCallRecorder r;
        CHECK(r.GetStatus() == DrawCallRecorder::Status::Idle);
        CHECK(!r.IsCapturing());
        // nothing is recorded until a capture is requested
        r.BeginFrame(1); CHECK(r.Record(Call("a", "p", "m", "x", "s", 1)) == -1); r.EndFrame();
        CHECK(r.Count() == 0 && !r.HasCapture());

        r.RequestCapture();
        CHECK(r.GetStatus() == DrawCallRecorder::Status::Armed);
        CHECK(!r.IsCapturing());                                   // armed, the frame has not begun
        CHECK(r.Record(Call("early", "p", "m", "x", "s", 1)) == -1);
        r.BeginFrame(42);
        CHECK(r.IsCapturing() && r.GetStatus() == DrawCallRecorder::Status::Capturing);
        CHECK(r.Record(Call("first", "p", "m", "x", "s", 1)) == 0);
        CHECK(r.Record(Call("second", "p", "m", "x", "s", 1)) == 1);
        CHECK(r.Count() == 0);                                     // the capture is not readable until the frame ends
        r.EndFrame();
        CHECK(!r.IsCapturing() && r.HasCapture() && r.GetStatus() == DrawCallRecorder::Status::Captured);
        CHECK(r.Count() == 2 && r.CapturedFrame() == 42);
        DrawCallRecord c;
        CHECK(r.Get(1, c) && c.name == "second" && c.order == 1);
        CHECK(!r.Get(2, c));
        // the next frame is not captured
        r.BeginFrame(43); CHECK(r.Record(Call("late", "p", "m", "x", "s", 1)) == -1); r.EndFrame();
        CHECK(r.Count() == 2 && r.CapturedFrame() == 42);

        // requesting again replaces the capture with the next frame
        r.RequestCapture();
        r.BeginFrame(50); r.Record(Call("only", "p", "m", "x", "s", 1)); r.EndFrame();
        CHECK(r.Count() == 1 && r.CapturedFrame() == 50);

        // cancel and clear
        r.RequestCapture(); r.CancelCapture();
        r.BeginFrame(51); r.Record(Call("nope", "p", "m", "x", "s", 1)); r.EndFrame();
        CHECK(r.CapturedFrame() == 50 && r.Count() == 1);
        r.ClearCapture();
        CHECK(r.Count() == 0 && r.GetStatus() == DrawCallRecorder::Status::Idle);
        // cancelling in the middle of a frame drops it
        r.RequestCapture(); r.BeginFrame(60); r.Record(Call("x", "p", "m", "x", "s", 1)); r.CancelCapture(); r.EndFrame();
        CHECK(!r.HasCapture() && !r.IsCapturing());
    }

    // ---- the call limit -------------------------------------------------------------
    {
        DrawCallRecorder r(3);
        r.RequestCapture(); r.BeginFrame(1);
        for (int i = 0; i < 5; ++i) r.Record(Call("c" + std::to_string(i), "p", "m", "x", "s", 1));
        r.EndFrame();
        CHECK(r.Count() == 3 && r.Truncated());
        CHECK(r.ExportJson().find("\"truncated\":true") != std::string::npos);
        r.SetMaxCalls(100);
        r.RequestCapture(); r.BeginFrame(2); r.Record(Call("c", "p", "m", "x", "s", 1)); r.EndFrame();
        CHECK(r.Count() == 1 && !r.Truncated());
    }

    // ---- hook installation ---------------------------------------------------------------
    {
        CHECK(ActiveFrameDebugHook() == nullptr);
        DrawCallRecorder r;
        SetActiveFrameDebugHook(&r);
        r.RequestCapture();
        // renderer side: only the interface
        IFrameDebugHook* hook = ActiveFrameDebugHook();
        CHECK(hook == &r);
        hook->BeginFrame(7);
        if (hook->IsCapturing()) hook->Record(Call("viaHook", "p", "m", "x", "s", 1));
        hook->EndFrame();
        SetActiveFrameDebugHook(nullptr);
        CHECK(ActiveFrameDebugHook() == nullptr);
        CHECK(r.Count() == 1 && r.CapturedFrame() == 7);
    }

    // ---- step through ---------------------------------------------------------------------
    {
        DrawCallRecorder r;
        r.RequestCapture(); CaptureSampleFrame(r, 9);
        CHECK(r.Count() == 8);
        CHECK(r.BreakAfter() == -1 && r.ExecutedCount() == 8 && r.CurrentIndex() == 7);
        CHECK(!r.StepForward());                                   // no break: nothing to step
        r.SetBreakAfter(2);
        CHECK(r.BreakAfter() == 2 && r.ExecutedCount() == 3 && r.CurrentIndex() == 2);
        CHECK(r.IsExecuted(2) && !r.IsExecuted(3));
        CHECK(r.StepForward() && r.BreakAfter() == 3 && r.ExecutedCount() == 4);
        CHECK(r.StepBack() && r.StepBack() && r.StepBack() && r.BreakAfter() == 0);
        CHECK(!r.StepBack() && r.BreakAfter() == 0);               // stops at the first call
        r.StepToEnd();
        CHECK(r.BreakAfter() == -1);
        CHECK(r.StepBack() && r.BreakAfter() == 6);                // from "everything" to one before the last
        r.SetBreakAfter(1000);                                     // clamps to the last call
        CHECK(r.BreakAfter() == 7 && r.ExecutedCount() == 8);
        CHECK(!r.StepForward());
        r.SetBreakAfter(-5);
        CHECK(r.BreakAfter() == -1);
        r.StepToStart();
        CHECK(r.BreakAfter() == 0 && r.ExecutedCount() == 1);

        // replay: the renderer asks before each draw
        r.SetBreakAfter(3);
        CHECK(!r.ShouldSkipDraw(5));                               // not replaying: never skip
        r.BeginReplay();
        CHECK(r.Replaying());
        CHECK(!r.ShouldSkipDraw(0) && !r.ShouldSkipDraw(3) && r.ShouldSkipDraw(4) && r.ShouldSkipDraw(7));
        r.ClearBreak();
        CHECK(!r.ShouldSkipDraw(7));                               // no break: everything draws
        r.EndReplay();
        CHECK(!r.Replaying());

        // a new capture resets the break; stepping with nothing captured does nothing
        r.SetBreakAfter(2);
        r.RequestCapture(); CaptureSampleFrame(r, 10);
        CHECK(r.BreakAfter() == -1);
        DrawCallRecorder empty;
        empty.SetBreakAfter(3);
        CHECK(empty.BreakAfter() == -1 && !empty.StepForward() && !empty.StepBack() && empty.CurrentIndex() == -1);
    }

    // ---- filters ---------------------------------------------------------------------------------
    {
        DrawCallRecorder r;
        r.RequestCapture(); CaptureSampleFrame(r, 1);
        DrawCallFilter f;
        CHECK(r.Filter(f).size() == 8);                            // empty filter: everything, in drawing order
        std::vector<std::uint32_t> all = r.Filter(f);
        bool ordered = true; for (size_t i = 0; i < all.size(); ++i) if (all[i] != i) ordered = false;
        CHECK(ordered);

        f.text = "crate";                                          // case insensitive, matches name, path and so on
        CHECK(r.Filter(f).size() == 3);                            // Crate x2 in shadows/opaque + Crate2
        f.caseSensitive = true;
        CHECK(r.Filter(f).empty());
        f.text = "Crate"; CHECK(r.Filter(f).size() == 3);
        f = DrawCallFilter(); f.text = "bloom_half"; CHECK(r.Filter(f).size() == 1);   // render target
        f = DrawCallFilter(); f.pass = "Opaque"; CHECK(r.Filter(f).size() == 4);
        f = DrawCallFilter(); f.material = "wood"; CHECK(r.Filter(f).size() == 2);
        f = DrawCallFilter(); f.mesh = "rock"; CHECK(r.Filter(f).size() == 2);
        f = DrawCallFilter(); f.shader = "lit"; CHECK(r.Filter(f).size() == 3);        // exact: lit_alpha is another shader
        f = DrawCallFilter(); f.minTriangles = 100; CHECK(r.Filter(f).size() == 2);
        f = DrawCallFilter(); f.kindMask = DrawCallKindBit(DrawCallKind::Dispatch); CHECK(r.Filter(f).size() == 1);
        f.kindMask |= DrawCallKindBit(DrawCallKind::DrawIndexed); CHECK(r.Filter(f).size() == 8);
        f = DrawCallFilter(); f.pass = "Opaque"; f.material = "wood"; f.text = "2"; CHECK(r.Filter(f).size() == 1);   // all conditions together
        // executedOnly follows the break
        r.SetBreakAfter(3);
        f = DrawCallFilter(); f.executedOnly = true;
        CHECK(r.Filter(f).size() == 4);
        f.pass = "Opaque"; CHECK(r.Filter(f).size() == 2);
        CHECK(DrawCallRecorder().Filter(DrawCallFilter()).empty());
    }

    // ---- summary ------------------------------------------------------------------------------------
    {
        DrawCallRecorder r;
        r.RequestCapture(); CaptureSampleFrame(r, 1);
        DrawCallSummary s = r.Summarize();
        CHECK(s.calls == 8 && s.draws == 7 && s.dispatches == 1);
        CHECK(s.triangles == 12 + 500 + 12 + 12 + 500 + 2 + 2);
        // material changes between consecutive draws that name one: wood, wood, stone, glass, bloom = 4 (the first bind counts)
        CHECK(s.materialSwitches == 4);
        CHECK(s.shaderSwitches == 4);                              // depth, lit, lit_alpha, post (repeats are not switches)
        CHECK(s.renderTargetSwitches == 1);                        // into bloom_half
        CHECK(s.uniqueMaterials == 4 && s.uniqueMeshes == 4 && s.uniqueShaders == 4);
        r.SetBreakAfter(1);
        DrawCallSummary part = r.Summarize(true);
        CHECK(part.calls == 2 && part.draws == 2 && part.triangles == 512);
        CHECK(part.materialSwitches == 0 && part.shaderSwitches == 1);   // no material named in the shadow pass
        CHECK(!s.ToText().empty() && s.ToText().find("8 call(s)") != std::string::npos);
        CHECK(DrawCallRecorder().Summarize().calls == 0);
    }

    // ---- export ---------------------------------------------------------------------------------------
    {
        DrawCallRecorder r;
        CHECK(Balanced(r.ExportJson()));                           // nothing captured: still valid JSON
        r.RequestCapture(); CaptureSampleFrame(r, 12);
        std::string js = r.ExportJson();
        CHECK(Balanced(js));
        CHECK(CountOf(js, "\"order\":") == 8);
        CHECK(js.find("\"frame\":12") != std::string::npos && js.find("\"callCount\":8") != std::string::npos);
        CHECK(js.find("\"kind\":\"Dispatch\"") != std::string::npos && js.find("\"groups\":[8,8,1]") != std::string::npos);
        CHECK(js.find("\"u_opacity\":\"0.4\"") != std::string::npos);
        CHECK(js.find("\"blend\":\"alpha\"") != std::string::npos && js.find("\"depthWrite\":false") != std::string::npos);
        CHECK(js.find("\"object\":\"World/Rock\"") != std::string::npos);
        CHECK(js.find("\"renderTarget\":\"bloom_half\"") != std::string::npos);
        CHECK(js.find("\"breakAfter\":-1") != std::string::npos);

        DrawCallFilter f; f.pass = "Shadows";
        std::string filtered = r.ExportJson(&f);
        CHECK(Balanced(filtered) && CountOf(filtered, "\"order\":") == 2 && filtered.find("\"callCount\":8") != std::string::npos);

        // names with quotes are escaped
        DrawCallRecorder q; q.RequestCapture(); q.BeginFrame(1);
        q.Record(Call("say \"hi\"\n", "p", "m", "x", "s", 1)); q.EndFrame();
        std::string qj = q.ExportJson();
        CHECK(Balanced(qj) && qj.find("say \\\"hi\\\"\\n") != std::string::npos);

        std::string text = r.ExportText();
        CHECK(CountOf(text, "\n") == 9 && text.find("Dispatch") != std::string::npos);
        CHECK(CountOf(r.ExportText(&f), "\n") == 3);
        r.SetBreakAfter(4);
        CHECK(r.ExportJson().find("\"breakAfter\":4") != std::string::npos);
    }

#ifndef _WIN32
    // ---- thread safety smoke test: a capture taken while other threads poll the state ------------------
    {
        DrawCallRecorder r(1000000);
        r.RequestCapture();
        r.BeginFrame(1);
        const int kThreads = 4, kCalls = 2000;
        std::vector<std::thread> th;
        for (int t = 0; t < kThreads; ++t)
            th.emplace_back([&r, kCalls, t] {
                for (int i = 0; i < kCalls; ++i) {
                    if (r.IsCapturing()) r.Record(Call("t" + std::to_string(t), "p", "m", "x", "s", 1));
                    r.ShouldSkipDraw((std::uint32_t)i);
                }
            });
        for (auto& x : th) x.join();
        r.EndFrame();
        CHECK(r.Count() == (size_t)kThreads * kCalls);
        // every order is unique and in sequence
        DrawCapture cap = r.Snapshot();
        bool seq = true;
        for (size_t i = 0; i < cap.calls.size(); ++i) if (cap.calls[i].order != i) seq = false;
        CHECK(seq);
    }
#endif

    TEST_MAIN_RESULT();
}
