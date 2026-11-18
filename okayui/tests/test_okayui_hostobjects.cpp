// Window mode: the Hierarchy and Inspector (editor/OkayUIHostObjects.cpp) over the REAL EditorState and a real okay::Scene, run through the real frame loop with
// a fake backend and a software painter. The Inspector draws the property registry's cards with the editor's own field code (InspectorPanel.cpp compiled
// against OkayUI). Docs 6.24 step 9.
#define OKAY_HAVE_OKAYUI 1
#define OKAYUI_HOST_PLATFORM 1
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIFrameLoop.hpp"
#include "OkayUIHostObjects.hpp"
#include "FakePlatformBackend.hpp"
#include "EditorState.hpp"
#include "okay/UI/OkayUIDock.hpp"

#include <cstdlib>
#include <string>
#include <vector>

namespace oked { void ConsoleLog(const std::string&, int) {} }   // main.cpp's Console, not linked here

namespace UI = OkayUI;
namespace plat = okay::platform;
using fakeplat::FakeBackend; using fakeplat::Move; using fakeplat::Btn;

int main() {
    RUN_SUITE("okayui window mode: Hierarchy and Inspector");
    okay::editor::EditorState ed;
    ed.NewScene3D();
    okay::GameObject* cube = ed.CreateCube("Crate");
    okay::GameObject* empty = ed.CreateEmpty("Holder");
    ed.Select(cube);
    const std::size_t startObjects = ed.scene().Objects().size();
    CHECK(startObjects >= 3);

    okayuitest::Reset();
    auto painter = UI::soft::CreateBitmapPainter(1100, 700);
    FakeBackend be; okayuihost::platform::FrameLoop loop(be, *painter);
    oked::HostModeState st;
    oked::wm::AddObjectPanels(st.panels, &st, &ed);
    CHECK(st.panels.hierarchy && st.panels.inspector);
    auto frame = [&] { be.now += 16; return loop.Frame([&] { oked::DrawHostModeUi(st); }); };
    auto click = [&](const UI::Rect& r) {
        const int x = (int)(r.x + r.w * 0.5f), y = (int)(r.y + r.h * 0.5f);
        be.queue.push_back(Move(x, y)); frame(); be.queue.push_back(Btn(true, x, y)); frame(); be.queue.push_back(Btn(false, x, y)); frame(); frame();
    };
    for (int i = 0; i < 6; ++i) CHECK(frame());
    const UI::DockLayout& L = UI::GetDockLayout();
    std::string why; CHECK(L.Validate(&why) && L.Contains(oked::kPanelHierarchy) && L.Contains(oked::kPanelInspector));
    CHECK(L.NodeOf(oked::kPanelHierarchy) != L.NodeOf(oked::kPanelInspector));

    // the Hierarchy lists the scene's root objects; the Inspector shows the selected object
    const oked::wm::ObjectsProbe& pr = oked::wm::GetObjectsProbe();
    CHECK(pr.hierarchyRows >= 3 && pr.firstRow.w > 20.0f && pr.createButton.w > 5.0f);
    CHECK(pr.selectedName == "Crate");
    CHECK(pr.inspectorCards >= 3);      // the object block, Transform and the mesh renderer (at least)
    CHECK(pr.fieldsDrawn >= 8);         // name, active, tag, layer ..., position, rotation, scale ...
    const UI::DrawData dd = UI::GetDrawData();
    CHECK(dd.vertexCount > 3000 && dd.dropped == 0);
    if (const char* shot = std::getenv("OKAYUI_SHOT")) { std::vector<std::uint8_t> px; int w = 0, h = 0; if (painter->ReadPixels(px, w, h)) okay::ImageWrite::WritePNG(shot, w, h, 4, px.data()); }

    // clicking another row selects it and the Inspector follows
    const UI::Rect r0 = pr.firstRow;
    click(UI::Rect{r0.x, r0.y + r0.h * 1.0f, r0.w, r0.h});   // the second row
    CHECK(ed.selected() != nullptr && ed.selected() != cube);
    frame();
    CHECK(oked::wm::GetObjectsProbe().selectedName == ed.selected()->name);
    // the property edit goes through the registry: the "active" check box of the object
    ed.Select(cube); for (int i = 0; i < 3; ++i) frame();
    const bool wasActive = cube->active;
    click(pr.activeField);
    CHECK(cube->active == !wasActive);
    CHECK(ed.dirty);
    click(pr.activeField);
    CHECK(cube->active == wasActive);
    // + opens the create menu: Empty adds an object
    const std::size_t before = ed.scene().Objects().size();
    click(pr.createButton);
    for (int i = 0; i < 2; ++i) frame();
    CHECK(pr.createEmpty.w > 20.0f);
    click(pr.createEmpty);
    CHECK(ed.scene().Objects().size() == before + 1);
    // nothing selected: the Inspector says so
    ed.Select(nullptr); for (int i = 0; i < 3; ++i) frame();
    CHECK(oked::wm::GetObjectsProbe().selectedName.empty() && oked::wm::GetObjectsProbe().inspectorCards == 0);
    (void)empty;
    TEST_MAIN_RESULT();
}
