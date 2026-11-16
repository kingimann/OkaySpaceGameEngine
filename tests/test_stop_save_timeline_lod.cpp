// Three editor bugs from issue #888, each checked headless:
//  85  Save in the "Save your work before stopping?" prompt writes the edit-time scene (it used to skip
//      the scene because Play was still running), and the scene is no longer unsaved.
//  76  Dragging a Timeline key past another key on the same track keeps both keys (each drag step used
//      to replace whatever key sat at the new time).
//  77  A LOD Group in distance mode switches at the typed distances on big objects too (distances under
//      about 0.87 x the object's size used to be clamped away).
#include "test_framework.hpp"
#include "EditorState.hpp"
#include "SaveBeforePrompt.hpp"
#include "TimelineKeyMove.hpp"
#include "okay/Optimize/LodGroup.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace okay;
using okay::editor::EditorState;
using okay::editor::PendingAction;
using okay::optimize::LodView;
using okay::optimize::LodSelection;

namespace {

std::string ReadFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

// What editor/main.cpp's SaveProject(ed, interactive = true) does with the scene: while the game runs it
// leaves the scene alone (saving would write the running copy); otherwise it writes the scene's file.
bool SaveLikeTheEditor(EditorState& ed, const std::string& path, bool& sceneWritten) {
    sceneWritten = false;
    if (ed.isPlaying()) return true;
    sceneWritten = ed.Save(path);
    return sceneWritten;
}

// The window's drag: one Move per frame the snapped mouse time changes. Returns the dragged key's time.
template <class MoveFn> float DragFrames(float from, float to, int fps, MoveFn move) {
    float cur = from;
    const int a = (int)std::lround(from * fps), b = (int)std::lround(to * fps);
    const int step = b >= a ? 1 : -1;
    for (int f = a + step; f != b + step; f += step) {
        const float nt = Sequence::SnapToFrame((float)f / (float)fps, fps);
        if (move(cur, nt)) cur = nt;
    }
    return cur;
}

// Walk the camera from `near` to `far` (or back) feeding the state back; the distance where `level`
// first shows (-1 = never).
float FirstDistanceOf(const std::vector<float>& d, float size, int level, float nearD, float farD, float step) {
    LodView view{1080.0f, 60.0f};
    int state = -1;
    const bool out = farD > nearD;
    for (float x = nearD; out ? x <= farD : x >= farD; x += out ? step : -step) {
        const LodSelection s = optimize::SelectLodByDistance(d, 0.0f, 0.1f, x, state, size, view);
        state = s.state;
        if (s.level == level) return x;
    }
    return -1.0f;
}

}  // namespace

int main() {
    RUN_SUITE("stop_save_timeline_lod");
    Log::console = false;
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "okay_test_stop_save";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
    const std::string scenePath = (dir / "five.okayscene").string();

    // ---- 85: Save in the Stop prompt saves the edit-time scene ------------------------------------------------
    {
        EditorState ed;
        ed.scene().CreateGameObject("Cube");
        CHECK(ed.Save(scenePath));
        ed.scene().CreateGameObject("Cube (1)");          // the edit (Ctrl+D) the finding made before Play
        CHECK(ed.HasUnsavedChanges());
        ed.Play();                                         // "Don't Save" on the Play prompt
        CHECK(ed.isPlaying());
        ed.scene().CreateGameObject("PlayOnly");           // made while playing: thrown away by Stop
        CHECK(ed.HasUnsavedChanges());                     // so Stop asks
        bool written = false, ran = false;
        const bool ok = okay::editor::RunSaveBefore(PendingAction::Stop, ed.isPlaying(),
            [&] { ed.Stop(); },
            [&] { return SaveLikeTheEditor(ed, scenePath, written); },
            [&] { ed.Stop(); ran = true; });
        CHECK(ok);
        CHECK(!ed.isPlaying());                            // Play stopped
        CHECK(written);                                    // the scene was written
        const std::string disk = ReadFile(scenePath);
        CHECK(disk.find("Cube (1)") != std::string::npos); // with the edit
        CHECK(disk.find("PlayOnly") == std::string::npos); // and not the running game's copy
        CHECK(!ed.dirty);
        CHECK(!ed.HasUnsavedChanges());                    // so the next Play doesn't ask again
        (void)ran;
    }
    // ---- 85: the same for opening another scene while playing (Load would replace the unsaved edit) ----------
    {
        EditorState ed;
        ed.scene().CreateGameObject("Cube");
        CHECK(ed.Save(scenePath));
        ed.scene().CreateGameObject("Edited");
        ed.Play();
        bool written = false, ran = false;
        okay::editor::RunSaveBefore(PendingAction::LoadScene, ed.isPlaying(),
            [&] { ed.Stop(); },
            [&] { return SaveLikeTheEditor(ed, scenePath, written); },
            [&] { ran = true; });
        CHECK(written && ran && !ed.isPlaying());
        CHECK(ReadFile(scenePath).find("Edited") != std::string::npos);
    }
    // ---- 85: outside Play the prompt still saves, then runs the action ---------------------------------------
    {
        const auto plan = okay::editor::PlanSaveBefore(PendingAction::Play, false);
        CHECK(!plan.stopFirst && plan.runActionAfter);
        const auto load = okay::editor::PlanSaveBefore(PendingAction::LoadScene, false);
        CHECK(!load.stopFirst && load.runActionAfter);
    }

    // ---- 76: drag a Transform key past another key: both stay ------------------------------------------------
    {
        SeqTransformTrack tr;
        for (float t : {0.0f, 1.0f, 2.0f}) {
            tr.position.push_back({t, Vec3(t * 5.0f, 0, 0)});
            tr.rotation.push_back({t, Vec3(0, t * 10.0f, 0)});
            tr.scale.push_back({t, Vec3(1, 1, 1)});
        }
        const float end = DragFrames(0.0f, 1.5f, 30, [&](float a, float b) { return oked::tlkeys::MoveTransformKeys(tr, a, b); });
        CHECK_NEAR(end, 1.5f, 1e-3);
        CHECK(tr.position.size() == 3 && tr.rotation.size() == 3 && tr.scale.size() == 3);
        const int mid = Sequence::KeyAt(tr.position, 1.0f);
        CHECK(mid >= 0 && mid < (int)tr.position.size() && std::fabs(tr.position[(std::size_t)(mid < 0 ? 0 : mid)].value.x - 5.0f) < 1e-4f);
        const int moved = Sequence::KeyAt(tr.position, 1.5f);
        CHECK(moved >= 0 && std::fabs(tr.position[(std::size_t)(moved < 0 ? 0 : moved)].value.x) < 1e-4f);
        CHECK(Sequence::KeyAt(tr.rotation, 1.0f) >= 0 && Sequence::KeyAt(tr.scale, 1.0f) >= 0);
        for (std::size_t i = 1; i < tr.position.size(); ++i) CHECK(tr.position[i - 1].time < tr.position[i].time);
        // Back the other way, past both keys.
        const float back = DragFrames(1.5f, 0.0f, 30, [&](float a, float b) { return oked::tlkeys::MoveTransformKeys(tr, a, b); });
        (void)back;
        CHECK(tr.position.size() == 3);
        // Typing the time of another key into the Time field: refused, nothing lost.
        const std::size_t before = tr.position.size();
        oked::tlkeys::MoveTransformKeys(tr, 2.0f, 1.0f);
        CHECK(tr.position.size() == before && Sequence::KeyAt(tr.position, 2.0f) >= 0);
    }
    // ---- 76: the same on a Letterbox (float) and a Fade (colour) track ----------------------------------------
    {
        std::vector<SeqFloatKey> lb{{0.0f, 0.0f}, {1.0f, 0.12f}, {2.0f, 0.0f}};
        DragFrames(0.0f, 1.5f, 30, [&](float a, float b) { return oked::tlkeys::MoveFloatKeys(lb, a, b); });
        CHECK(lb.size() == 3);
        const int mid = Sequence::FloatKeyAt(lb, 1.0f);
        CHECK(mid >= 0 && std::fabs(lb[(std::size_t)(mid < 0 ? 0 : mid)].value - 0.12f) < 1e-6f);

        std::vector<SeqColorKey> fd{{0.0f, Color(0, 0, 0, 0)}, {1.0f, Color(0, 0, 0, 1)}, {2.0f, Color(0, 0, 0, 0)}};
        DragFrames(2.0f, 0.5f, 30, [&](float a, float b) { return oked::tlkeys::MoveColorKeys(fd, a, b); });
        CHECK(fd.size() == 3);
        CHECK(Sequence::ColorKeyAt(fd, 1.0f) >= 0 && Sequence::ColorKeyAt(fd, 0.5f) >= 0);
    }

    // ---- 77: distance mode follows the typed distances whatever the object's size ----------------------------
    {
        const std::vector<float> d{5.0f, 15.0f, 40.0f};   // three levels: leave L0 past 5 m, L1 past 15 m
        for (float size : {1.0f, 4.0f, 20.0f, 100.0f}) {
            const float l1 = FirstDistanceOf(d, size, 1, 0.1f, 200.0f, 0.05f);
            const float l2 = FirstDistanceOf(d, size, 2, 0.1f, 400.0f, 0.05f);
            std::cout << "  size " << size << ": L1 from " << l1 << " m, L2 from " << l2 << " m\n";
            // 10 % hysteresis: walking out, a level is left at d / (1 - 0.1).
            CHECK(l1 >= 5.0f && l1 <= 5.0f / 0.9f + 0.1f);
            CHECK(l2 >= 15.0f && l2 <= 15.0f / 0.9f + 0.1f);
            // Walking back in, L0 returns below d / (1 + 0.1).
            const float l0 = FirstDistanceOf(d, size, 0, 30.0f, 0.1f, 0.05f);
            CHECK(l0 <= 5.0f && l0 >= 5.0f / 1.1f - 0.1f);
        }
        // A cull distance past the last switch culls there.
        LodView view{1080.0f, 60.0f};
        int state = -1; bool culled = false; float at = -1.0f;
        for (float x = 1.0f; x < 200.0f && !culled; x += 0.05f) {
            const LodSelection s = optimize::SelectLodByDistance({5.0f, 15.0f, 40.0f}, 60.0f, 0.1f, x, state, 50.0f, view);
            state = s.state;
            if (s.culled) { culled = true; at = x; }
        }
        std::cout << "  size 50, cull 60 m: culled from " << at << " m\n";
        CHECK(culled && at >= 60.0f && at <= 60.0f / 0.9f + 0.1f);
    }

    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
